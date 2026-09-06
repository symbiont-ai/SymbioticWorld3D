"""The lab meeting (PRD F2/F3): a structured state machine
INGEST -> EVIDENCE -> HYPOTHESES -> STANCES -> (CRUX | DISSENT) -> DESIGN -> DECIDE -> COMMIT
with fixed turn order, hard caps, and every discourse rule enforced in code:

- agents may only cite evidence IDs that exist (invalid citations are dropped);
- an agree with no surviving citation is recorded as abstain (downgraded);
- a disagree needs a citation or a named prior, else abstain (downgraded);
- a stance split becomes a double-crux and, if measurable, a docketed dispute;
- unanimous agreement appoints a rotating dissenter (excluding the proposer);
- protocols are validated and clamped in code; Karla's veto is checked in code;
- before an experiment is queued every agent preregisters a prediction.

Credibility enters at the DESIGN round: when several disputes compete for the
one protocol slot, the docket is ranked by credibility-weighted interest
(sum of vote weights of agents holding a non-abstain stance on the card).
"""
import json

from . import config, conservation, db, evidence, memory, scorer
from .profiles import OBSERVERS, TURN_ORDER
from .runner import METRICS


class MeetingRunner:
    def __init__(self, con, profiles, llm, log=print):
        self.con, self.profiles, self.llm, self.log = con, profiles, llm, log

    def _turn(self, agent, kind, prompt, ctx):
        prof = self.profiles[agent]
        try:
            return self.llm.turn(prof, kind, prompt, ctx=ctx)
        except TypeError:
            return self.llm.turn(prof, kind, prompt)

    # ------------------------------------------------------------------ rounds

    def run(self, kind="regular"):
        con = self.con
        cur = con.execute("INSERT INTO meetings(started_at, kind, minutes) VALUES(?,?,'')",
                          (db.now(), kind))
        mid = cur.lastrowid
        self.log(f"\n=== MEETING {mid} ({kind}) ===")

        digest = self._ingest(mid)
        conservation.ensure_programs(con, mid, self.log)   # endangerment dockets
        findings = self._evidence_round(mid, digest)
        new_cards = self._hypotheses_round(mid, digest, findings)
        cards = memory.active_cards(con, config.MAX_ACTIVE_CARDS_PER_MEETING)
        stance_map = self._stance_round(mid, cards, digest)
        disputes = self._crux_or_dissent(mid, cards, stance_map)
        conservation.advance(con, self, mid, stance_map, self.log)
        exp_id = self._design_round(mid, disputes, cards, stance_map)
        self._commit(mid, findings, new_cards, cards, stance_map, disputes, exp_id)
        con.commit()
        return mid

    def _ingest(self, mid):
        new = evidence.ingest_all(self.con)
        new += evidence.cross_run_stats(self.con)
        if new:
            self.log(f"  [ingest] {len(new)} new evidence objects")
        # open the meeting with the results docket from experiments run since last time
        for row in self.con.execute(
                "SELECT id, verdict, metric_result FROM experiments WHERE status='done'"):
            self.log(f"  [docket] {row['id']} verdict={row['verdict']}")
        return evidence.evidence_digest(self.con)

    def _evidence_round(self, mid, digest):
        findings = []
        if not digest:
            self.log("  [evidence] no runs ingested yet; observers have nothing to cite")
            return findings
        ev_text = "\n".join(digest)
        for agent in OBSERVERS:
            # competitive advantage: credible observers hold more floor time
            weight = scorer.vote_weight(self.con, agent, self.profiles[agent].domains)
            quota = config.MAX_FINDINGS_PER_OBSERVER if weight >= 1.0 \
                else config.MAX_FINDINGS_PER_OBSERVER - 1
            prompt = (f"ROUND 1 — EVIDENCE. Present up to {quota} "
                      f"findings from your domain, each anchored to an evidence ID.\n"
                      f"Available evidence:\n{ev_text}")
            raw = self._turn(agent, "findings", prompt, {"evidence": digest})
            kept = []
            for f in raw.get("findings", [])[:quota]:
                ids = [e for e in f.get("evidence_ids", []) if db.evidence_exists(self.con, e)]
                if ids and f.get("text"):
                    kept.append({"agent": agent, "text": f["text"], "evidence_ids": ids})
            db.log_turn(self.con, mid, "EVIDENCE", agent, kept)
            findings += kept
            self.log(f"  [evidence] {agent}: {len(kept)} finding(s)")
        return findings

    def _candidate_claims(self):
        """Open questions and seeded conjectures not yet on a card."""
        carded = {r["claim"] for r in self.con.execute("SELECT claim FROM hypotheses")}
        out = []
        for q in self.con.execute("SELECT * FROM open_questions WHERE status='open'"):
            claim = q["text"]
            if claim not in carded:
                out.append((claim, "mechanism to be argued", f"origin: {q['origin']}"))
        return out

    def _hypotheses_round(self, mid, digest, findings):
        new_ids = []
        candidates = self._candidate_claims()
        cited = sorted({e for f in findings for e in f["evidence_ids"]})
        find_text = "\n".join(f"- ({f['agent']}) {f['text']} [{','.join(f['evidence_ids'])}]"
                              for f in findings) or "(none presented)"
        cand_text = "\n".join(f"- {c[0]}" for c in candidates) or "(none)"
        prompt = (f"ROUND 2 — HYPOTHESES. Propose up to "
                  f"{config.MAX_NEW_HYPOTHESES_PER_MEETING} hypothesis cards: claim, "
                  f"proposed mechanism, scope conditions, cited evidence IDs.\n"
                  f"This meeting's findings:\n{find_text}\n"
                  f"Standing open questions you may turn into cards:\n{cand_text}")
        raw = self._turn("Ada", "hypotheses",  prompt,
                         {"candidate_claims": candidates, "cited": cited})
        for p in raw.get("proposals", [])[:config.MAX_NEW_HYPOTHESES_PER_MEETING]:
            if not p.get("claim"):
                continue
            ids = [e for e in p.get("evidence_ids", []) if db.evidence_exists(self.con, e)]
            hid = memory.create_card(self.con, p["claim"], p.get("mechanism", ""),
                                     p.get("scope", ""), "Ada", mid, evidence_ids=ids)
            new_ids.append(hid)
            self.log(f"  [hypotheses] Ada proposes {hid}: {p['claim'][:70]}")
        db.log_turn(self.con, mid, "HYPOTHESES", "Ada", raw)
        return new_ids

    def _validate_stance(self, raw):
        stance = raw.get("stance", "abstain")
        conf = max(0.0, min(1.0, float(raw.get("confidence", 0.5))))
        reason = raw.get("reason", "")
        ev = [e for e in raw.get("evidence_ids", []) if db.evidence_exists(self.con, e)]
        downgraded = 0
        if stance == "agree" and not ev:
            stance, downgraded = "abstain", 1           # no free consensus
        if stance == "disagree" and not ev and "prior" not in reason.lower():
            stance, downgraded = "abstain", 1
        return stance, conf, reason, ev, downgraded

    def _stance_round(self, mid, cards, digest):
        stance_map = {}   # hid -> {agent: (stance, conf)}
        ev_text = "\n".join(digest[:20])
        for card in cards:
            hid = card["id"]
            linked = memory.card_evidence_ids(self.con, hid)
            per_card = {}
            for agent in TURN_ORDER:
                if agent == "Archie":
                    continue                             # the moderator holds no stances
                prompt = (f"ROUND 3 — STANCE on {hid}: \"{card['claim']}\" "
                          f"(mechanism: {card['mechanism']}; status: {card['status']}).\n"
                          f"Register stance agree/disagree/abstain with confidence 0-1 and a "
                          f"reason citing at least one evidence ID or a named prior.\n"
                          f"Evidence linked to this card: {', '.join(linked) or 'none'}\n"
                          f"Recent evidence:\n{ev_text}")
                raw = self._turn(agent, "stance", prompt,
                                 {"claim": card["claim"],
                                  "evidence_ids": linked or [d.split(":")[0] for d in digest[:2]]})
                stance, conf, reason, ev, down = self._validate_stance(raw)
                self.con.execute(
                    "INSERT INTO stances(meeting_id, hypothesis_id, agent, stance, confidence,"
                    " reason, evidence_ids, downgraded) VALUES(?,?,?,?,?,?,?,?)",
                    (mid, hid, agent, stance, conf, reason, json.dumps(ev), down))
                per_card[agent] = (stance, conf)
            tally = {s: sum(1 for v in per_card.values() if v[0] == s)
                     for s in ("agree", "disagree", "abstain")}
            self.log(f"  [stances] {hid}: {tally['agree']} agree / "
                     f"{tally['disagree']} disagree / {tally['abstain']} abstain")
            stance_map[hid] = per_card
        return stance_map

    def _pending_experiment(self, hid):
        return self.con.execute(
            "SELECT 1 FROM experiments WHERE hypothesis_id=? AND status IN "
            "('queued','awaiting-sim')", (hid,)).fetchone() is not None

    def _crux_or_dissent(self, mid, cards, stance_map):
        disputes = []
        for card in cards:
            hid = card["id"]
            if card["proposer"] == "Docket":
                continue   # conservation cards resolve through their program stages
            if self._pending_experiment(hid):
                self.log(f"  [crux] {hid}: experiment already pending; dispute stays docketed")
                continue
            per = stance_map.get(hid, {})
            agrees = [(a, c) for a, (s, c) in per.items() if s == "agree"]
            disagrees = [(a, c) for a, (s, c) in per.items() if s == "disagree"]
            if agrees and disagrees:
                d_id = self._run_crux(mid, card, agrees, disagrees)
                if d_id:
                    disputes.append(d_id)
            elif agrees and not disagrees:
                self._appoint_dissenter(mid, card)
        return disputes

    def _run_crux(self, mid, card, agrees, disagrees):
        hid = card["id"]
        side_a = max(agrees, key=lambda x: x[1])[0]
        side_b = max(disagrees, key=lambda x: x[1])[0]
        cruxes = {}
        for agent in (side_a, side_b):
            prompt = (f"ROUND 4 — DOUBLE CRUX on {hid}: \"{card['claim']}\".\n"
                      f"You hold a stance on this card. State the single observable that "
                      f"would change your mind, whether it is measurable in the sim, and "
                      f"which registered metric captures it. Allowed metrics: "
                      f"{', '.join(METRICS)}")
            cruxes[agent] = self._turn(agent, "crux", prompt, {"claim": card["claim"]})
            db.log_turn(self.con, mid, "CRUX", agent, cruxes[agent])
        chosen = next((c for c in cruxes.values()
                       if c.get("measurable") and c.get("metric") in METRICS), None)
        d_id = db.next_id(self.con, "disputes", "D")
        sides = json.dumps({"agree": [a for a, _ in agrees],
                            "disagree": [a for a, _ in disagrees]})
        if chosen:
            self.con.execute("INSERT INTO disputes VALUES(?,?,?,?,?,?)",
                             (d_id, hid, sides, json.dumps(chosen), None, "open"))
            self.log(f"  [crux] {hid}: {side_a} vs {side_b} -> dispute {d_id} "
                     f"docketed on metric {chosen['metric']}")
            return d_id
        self.con.execute("INSERT INTO disputes VALUES(?,?,?,?,?,?)",
                         (d_id, hid, sides, json.dumps(list(cruxes.values())), None,
                          "unresolvable-in-sim"))
        self.log(f"  [crux] {hid}: no measurable crux -> logged unresolvable-in-sim")
        return None

    def _appoint_dissenter(self, mid, card):
        hid = card["id"]
        candidates = [a for a in TURN_ORDER
                      if a not in ("Archie", card["proposer"])]
        dissenter = candidates[mid % len(candidates)]     # rotation by meeting number
        prompt = (f"You are this meeting's appointed dissenter for {hid}: "
                  f"\"{card['claim']}\" — every stance agreed, so produce the strongest "
                  f"objection anyway: the most plausible boring explanation or confound.")
        raw = self._turn(dissenter, "challenge", prompt, {"claim": card["claim"]})
        text = raw.get("text", "")
        self.con.execute(
            "INSERT INTO challenges(meeting_id, hypothesis_id, agent, text) VALUES(?,?,?,?)",
            (mid, hid, dissenter, text))
        db.log_turn(self.con, mid, "DISSENT", dissenter, raw)
        self.log(f"  [dissent] unanimity on {hid}; {dissenter} appointed dissenter")

    # ------------------------------------------------------------------ design

    def _interest(self, hid, stance_map):
        per = stance_map.get(hid, {})
        return sum(scorer.vote_weight(self.con, a, self.profiles[a].domains)
                   for a, (s, _) in per.items() if s != "abstain")

    def _pick_design_target(self, disputes, cards, stance_map):
        if disputes:
            rows = [self.con.execute("SELECT * FROM disputes WHERE id=?", (d,)).fetchone()
                    for d in disputes]
            rows.sort(key=lambda r: -self._interest(r["hypothesis_id"], stance_map))
            best = rows[0]
            card = self.con.execute("SELECT * FROM hypotheses WHERE id=?",
                                    (best["hypothesis_id"],)).fetchone()
            return card, best, json.loads(best["crux"])
        # fallback: the newest active card still lacking a queued/done experiment —
        # research-day behavior: open questions get experiments even before anyone
        # has evidence to argue over.
        for card in cards:
            if card["proposer"] == "Docket":
                continue   # program-managed; never the free design slot's target
            has = self.con.execute(
                "SELECT 1 FROM experiments WHERE hypothesis_id=? AND status IN "
                "('queued','awaiting-sim','done')", (card["id"],)).fetchone()
            if not has:
                return card, None, None
        return None, None, None

    # (disputes passed in are fresh this meeting; cards with pending experiments
    # were already skipped in the crux round, so nothing is designed twice)

    def _validate_protocol(self, raw, crux):
        modes = {"A", "B", "C", "N"}
        proto = {
            "intervention_mode": raw.get("intervention_mode", "C").upper(),
            "intervention_set": raw.get("intervention_set", "") or "",
            "control_mode": raw.get("control_mode", "C").upper(),
            "control_set": raw.get("control_set", "") or "",
            "seeds": sorted({int(s) for s in raw.get("seeds", [1, 2])})[:5] or [1, 2],
            "duration": max(300.0, min(3600.0, float(raw.get("duration", 900)))),
            "metric": raw.get("metric", ""),
            "threshold": abs(float(raw.get("threshold", 0.01))) or 0.01,
            "rationale": raw.get("rationale", ""),
            # preregistered direction: the docketed crux wins; else Fisher's call
            "direction_if_claim_true": (crux or {}).get(
                "direction_if_claim_true",
                raw.get("direction_if_claim_true", "treatment_higher")),
        }
        if proto["direction_if_claim_true"] not in ("treatment_higher", "treatment_lower"):
            proto["direction_if_claim_true"] = "treatment_higher"
        if proto["intervention_mode"] not in modes or proto["control_mode"] not in modes:
            return None, "invalid mode"
        if len(proto["seeds"]) < 2:
            proto["seeds"] = [1, 2]
        if proto["metric"] not in METRICS:
            if crux and crux.get("metric") in METRICS:
                proto["metric"] = crux["metric"]
            else:
                return None, f"metric '{proto['metric']}' not in registry"
        if (proto["intervention_mode"] == proto["control_mode"]
                and proto["intervention_set"] == proto["control_set"]):
            return None, "intervention and control arms identical"
        return proto, None

    def _design_round(self, mid, disputes, cards, stance_map):
        card, dispute, crux = self._pick_design_target(disputes, cards, stance_map)
        if card is None:
            self.log("  [design] nothing to design this meeting")
            return None
        crux_text = json.dumps(crux) if crux else "(no docketed crux; design from the claim)"
        caveats = "\n".join(f"- {c['id']}: {c['claim']}" for c in memory.caveat_cards(self.con))
        prompt = (f"ROUND 5 — DESIGN. Draft one experiment protocol for {card['id']}: "
                  f"\"{card['claim']}\".\nDocketed crux: {crux_text}\n"
                  f"Intervention surface: modes A/B/C/N, seeds, duration (logical s), and "
                  f"-SWSet parameter overrides such as Settings.PatchRegenPerSec, "
                  f"Settings.DroughtRegenMultiplier, Lumen.ReproThreshold, "
                  f"Settings.WeightInteraction.\nAllowed metrics: {', '.join(METRICS)}\n"
                  f"Standing caveat cards you must not contradict:\n{caveats}")
        raw = self._turn("Fisher", "protocol", prompt,
                         {"claim": card["claim"], "crux": crux or {}})
        db.log_turn(self.con, mid, "DESIGN", "Fisher", raw)
        proto, err = self._validate_protocol(raw, crux)
        if err:
            self.log(f"  [design] Fisher's protocol rejected in code: {err}")
            return None

        exp_id = db.next_id(self.con, "experiments", "X")
        self.con.execute("INSERT INTO experiments VALUES(?,?,?,?,?,?,?,?)",
                         (exp_id, card["id"], json.dumps(proto), "proposed",
                          None, None, None, mid))

        # Karla review (her veto on running a flawed protocol is checked in code)
        review = self._turn("Karla", "review",
                            f"Review this protocol for {card['id']} (\"{card['claim']}\"): "
                            f"{json.dumps(proto)}. List objections; veto only for a missing "
                            f"control or an unfalsifiable design.",
                            {"protocol": proto, "claim": card["claim"]})
        db.log_turn(self.con, mid, "REVIEW", "Karla", review)
        if not review.get("approve", True) and review.get("veto_reason"):
            self.con.execute("UPDATE experiments SET status='vetoed' WHERE id=?", (exp_id,))
            self.log(f"  [design] {exp_id} VETOED by Karla: {review['veto_reason']}")
            return None

        # Preregistration: every agent predicts the outcome before the run.
        for agent in TURN_ORDER:
            if agent == "Archie":
                continue
            p = self._turn(agent, "prediction",
                           f"PREREGISTER your predicted outcome of {exp_id} "
                           f"(metric {proto['metric']}, treatment vs control, threshold "
                           f"{proto['threshold']}). The card under test: \"{card['claim']}\". "
                           f"If the claim is true the expected outcome is "
                           f"{proto['direction_if_claim_true']}.",
                           {"claim": card["claim"],
                            "direction_if_claim_true": proto["direction_if_claim_true"]})
            pred = p.get("predicted", "no_difference")
            if pred not in scorer.OUTCOMES:
                pred = "no_difference"
            conf = max(0.0, min(1.0, float(p.get("confidence", 0.5))))
            self.con.execute(
                "INSERT OR REPLACE INTO predictions(experiment_id, agent, predicted,"
                " confidence, actual, brier) VALUES(?,?,?,?,NULL,NULL)",
                (exp_id, agent, pred, conf))
        self.con.execute("UPDATE experiments SET status='queued' WHERE id=?", (exp_id,))
        if dispute:
            self.con.execute("UPDATE disputes SET experiment_id=? WHERE id=?",
                             (exp_id, dispute["id"]))
        self.log(f"  [design] {exp_id} queued for {card['id']} "
                 f"(metric {proto['metric']}, {len(proto['seeds'])} seeds x2 arms, "
                 f"{proto['duration']:.0f}s) with 6 preregistered predictions")
        return exp_id

    # ------------------------------------------------------------------ commit

    def _commit(self, mid, findings, new_cards, cards, stance_map, disputes, exp_id):
        lines = [f"Meeting {mid}."]
        if findings:
            lines.append(f"{len(findings)} findings presented "
                         f"({', '.join(sorted({f['agent'] for f in findings}))}).")
        if new_cards:
            lines.append(f"New cards: {', '.join(new_cards)}.")
        for hid, per in stance_map.items():
            t = {s: sum(1 for v in per.values() if v[0] == s)
                 for s in ("agree", "disagree", "abstain")}
            lines.append(f"{hid} stances: {t['agree']}A/{t['disagree']}D/{t['abstain']}Ab.")
        if disputes:
            lines.append(f"Disputes docketed: {', '.join(disputes)}.")
        if exp_id:
            lines.append(f"Experiment {exp_id} queued with preregistered predictions.")
        draft = " ".join(lines)
        raw = self._turn("Archie", "minutes",
                         f"Write 3-5 sentence minutes from this record: {draft}",
                         {"draft": draft})
        minutes = raw.get("summary", draft)
        self.con.execute("UPDATE meetings SET minutes=? WHERE id=?", (minutes, mid))
        db.log_turn(self.con, mid, "MINUTES", "Archie", {"summary": minutes})

        # notebooks: each agent records its own positions (persistence of personality)
        for agent in TURN_ORDER:
            if agent == "Archie":
                continue
            mine = [f"{hid}:{per[agent][0]}({per[agent][1]:.2f})"
                    for hid, per in stance_map.items() if agent in per]
            if mine:
                memory.append_notebook(self.con, agent,
                                       f"[meeting {mid}] stances: {', '.join(mine)}")
