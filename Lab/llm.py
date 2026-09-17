"""LLM backends. One shared model, many profiles (PRD F8).

- OllamaLLM: local llama3.1 via Ollama's /api/chat with structured outputs
  (`format` = JSON schema). Model is a config line.
- MockLLM: deterministic, profile-driven stand-in so the entire meeting/
  experiment/scoring loop runs (and is testable) with no model installed.
  Its outputs follow each profile's declared priors, so disagreement between
  profiles is real, just scripted.

Every turn returns a dict already parsed from JSON; callers validate content
(citation rules, caps, schema fields) in code — never trust the model.
"""
import hashlib
import json
import urllib.error
import urllib.request

from . import config

# --- schemas (Ollama structured outputs) ------------------------------------

def _obj(props, required=None):
    return {"type": "object", "properties": props,
            "required": required or list(props), "additionalProperties": False}

_S = lambda: {"type": "string"}
_N = lambda: {"type": "number"}
_B = lambda: {"type": "boolean"}
_A = lambda item: {"type": "array", "items": item}

SCHEMAS = {
    "findings": _obj({"findings": _A(_obj({
        "text": _S(), "evidence_ids": _A(_S())}))}),
    "hypotheses": _obj({"proposals": _A(_obj({
        "claim": _S(), "mechanism": _S(), "scope": _S(), "evidence_ids": _A(_S())}))}),
    "stance": _obj({
        "stance": {"type": "string", "enum": ["agree", "disagree", "abstain"]},
        "confidence": _N(), "reason": _S(), "evidence_ids": _A(_S())}),
    "crux": _obj({
        "observable": _S(), "measurable": _B(), "metric": _S(),
        "direction_if_claim_true": {"type": "string",
                                    "enum": ["treatment_higher", "treatment_lower"]}}),
    "protocol": _obj({
        "intervention_mode": _S(), "intervention_set": _S(),
        "control_mode": _S(), "control_set": _S(),
        "seeds": _A({"type": "integer"}), "duration": _N(),
        "metric": _S(), "threshold": _N(),
        "direction_if_claim_true": {"type": "string",
                                    "enum": ["treatment_higher", "treatment_lower"]},
        "rationale": _S()}),
    "review": _obj({"approve": _B(), "objections": _A(_S()), "veto_reason": _S()}),
    "prediction": _obj({
        "predicted": {"type": "string",
                      "enum": ["treatment_higher", "treatment_lower", "no_difference"]},
        "confidence": _N(), "reason": _S()}),
    "challenge": _obj({"text": _S()}),
    "minutes": _obj({"summary": _S()}),
    "agenda": _obj({"priorities": _A(_S()), "focus_metric": _S(), "rationale": _S()}),
    "arbitration": _obj({"uphold": _B(), "reason": _S()}),
}


class OllamaLLM:
    def __init__(self, model=None, url=None):
        self.model = model or config.OLLAMA_MODEL
        self.url = (url or config.OLLAMA_URL).rstrip("/")

    def available(self):
        try:
            urllib.request.urlopen(self.url + "/api/tags", timeout=3)
            return True
        except Exception:
            return False

    def turn(self, profile, kind, prompt):
        body = json.dumps({
            "model": self.model,
            "messages": [{"role": "system", "content": profile.system_prompt()},
                         {"role": "user", "content": prompt}],
            "format": SCHEMAS[kind],
            "options": {"temperature": profile.temperature},
            "stream": False,
        }).encode()
        req = urllib.request.Request(self.url + "/api/chat", data=body,
                                     headers={"Content-Type": "application/json"})
        for attempt in range(2):   # one retry on malformed JSON
            with urllib.request.urlopen(req, timeout=300) as r:
                content = json.loads(r.read())["message"]["content"]
            try:
                return json.loads(content)
            except json.JSONDecodeError:
                if attempt:
                    raise
        raise RuntimeError("unreachable")


class ClaudeCLILLM:
    """Generated (non-scripted) turns through the local `claude` CLI in print
    mode. Nothing is hardcoded: each profile is a system prompt, the schema is
    stated in the prompt, and the same code-side validation applies (invalid
    citations dropped, protocols clamped) — never trust the model. Model via
    LAB_CLAUDE_MODEL (default haiku for speed/cost)."""

    def __init__(self, model=None):
        import os
        import shutil
        self.exe = shutil.which("claude")
        self.model = model or os.environ.get("LAB_CLAUDE_MODEL",
                                             "claude-haiku-4-5-20251001")

    def available(self):
        return bool(self.exe)

    def turn(self, profile, kind, prompt, ctx=None):
        import subprocess
        schema = json.dumps(SCHEMAS[kind])
        full = (profile.system_prompt() + "\n\n" + prompt +
                "\n\nRespond with ONLY a JSON object matching this JSON schema "
                "(no prose, no code fences):\n" + schema)
        for attempt in range(2):
            out = subprocess.run(
                [self.exe, "-p", full, "--model", self.model],
                capture_output=True, text=True, timeout=300).stdout.strip()
            if out.startswith("```"):
                out = out.strip("`\n")
                out = out[out.find("{"):]
            try:
                start, end = out.index("{"), out.rindex("}") + 1
                return json.loads(out[start:end])
            except (ValueError, json.JSONDecodeError):
                if attempt:
                    raise RuntimeError(
                        f"claude CLI returned no parseable JSON for {kind}: {out[:200]}")


class OpenRouterLLM:
    """Hosted models via OpenRouter's OpenAI-compatible chat completions.
    One backend, many models: a profile may name its own `model:` in its YAML
    (per-scientist models), falling back to LAB_OPENROUTER_MODEL. Structured
    output is requested twice over — `response_format` json_schema where the
    hosted model supports it, and the schema restated in the prompt — and the
    same code-side validation applies regardless: never trust the model."""

    URL = "https://openrouter.ai/api/v1/chat/completions"

    def __init__(self, model=None, key=None):
        import os
        self.default_model = model or os.environ.get(
            "LAB_OPENROUTER_MODEL", "meta-llama/llama-3.1-8b-instruct")
        self.key = key or os.environ.get("OPENROUTER_API_KEY", "")

    def available(self):
        return bool(self.key)

    def turn(self, profile, kind, prompt, ctx=None):
        model = getattr(profile, "model", "") or self.default_model
        schema = SCHEMAS[kind]
        body = {
            "model": model,
            "messages": [
                {"role": "system", "content": profile.system_prompt()},
                {"role": "user", "content":
                    prompt + "\n\nRespond with ONLY a JSON object matching "
                    "this JSON schema (no prose, no code fences):\n"
                    + json.dumps(schema)},
            ],
            "temperature": profile.temperature,
            "response_format": {"type": "json_schema", "json_schema": {
                "name": kind, "strict": True, "schema": schema}},
        }
        import time
        out = ""
        for attempt in range(4):
            if attempt >= 2:
                body.pop("response_format", None)   # late tries: prompt only
            req = urllib.request.Request(
                self.URL, data=json.dumps(body).encode(),
                headers={"Content-Type": "application/json",
                         "Authorization": "Bearer " + self.key})
            try:
                with urllib.request.urlopen(req, timeout=300) as r:
                    reply = json.loads(r.read())
            except urllib.error.HTTPError as ex:
                # A model without json_schema support 4xxes the response_format;
                # drop it and retry rather than fail the turn.
                if ex.code < 500 and body.pop("response_format", None) is not None:
                    continue
                if ex.code >= 500 and attempt < 3:   # provider hiccup
                    time.sleep(2 * attempt + 1)
                    continue
                raise
            except OSError:
                # Connection reset / DNS blip / timeout: transient — an
                # always-on lab must outlive them, not die mid-meeting.
                if attempt < 3:
                    time.sleep(2 * attempt + 1)
                    continue
                raise
            out = (reply.get("choices") or [{}])[0].get("message", {}).get("content") or ""
            try:
                start, end = out.index("{"), out.rindex("}") + 1
                return json.loads(out[start:end])
            except (ValueError, json.JSONDecodeError):
                continue                            # malformed JSON: retry
        raise RuntimeError(
            f"OpenRouter/{model} returned no parseable JSON for {kind}: {out[:200]}")


# --- deterministic mock ------------------------------------------------------

def _h(*parts):
    return int(hashlib.md5("|".join(str(p) for p in parts).encode()).hexdigest(), 16)


def _match(text, keywords):
    t = text.lower()
    return sum(1 for k in keywords if k.lower() in t)


# The designer's bench: one row per kind of experiment this world can actually run, each carrying
# the two arms, the metric to preregister and the direction the claim implies if it is true. A claim
# takes the FIRST row whose `when` keyword it mentions (and, where `also` is set, one of those too).
# Before this table the designer was a three-branch keyword router: every challenge-card claim
# contains the word "drought", so one branch swallowed the docket and ten experiments came out as
# three distinct designs, six of them the same already-failed contrast. Mode A and every
# predator / mutation / novelty knob were unreachable, which is why the last unmet victory condition
# (OQ-VC-3, "the policy changed because of experience", missing its learning-off control) could not
# be proposed at all: a C-vs-C design with empty arms is vetoed in code as identical arms.
# arms = (intervention_mode, intervention_set, control_mode, control_set).
DESIGN_TEMPLATES = [
    dict(name="learning-off control",
         when=("learning-off", "learning off", "because of experience", "learned policy changed"),
         arms=("C", "", "A", ""),
         metric="lumen_q_drift_median", direction="treatment_higher", threshold=0.05,
         why="Mode A freezes the bandit while everything else is held equal: if lifetime learning "
             "is what moves the policy, Q-drift has to collapse in the control arm."),
    dict(name="predation selects exploration",
         when=("predat", "leviathan"), also=("epsilon", "avoid", "explor"),
         arms=("C", "Settings.bLeviathan=1", "C", "Settings.bLeviathan=0"),
         metric="lumen_end_mean_epsilon", direction="treatment_higher", threshold=0.01,
         why="A strike radius that removes foragers should price hesitation: if predation "
             "selects at all, the surviving lineages carry higher epsilon."),
    dict(name="predation sets the population floor",
         when=("predat", "leviathan"),
         arms=("C", "Settings.bLeviathan=1", "C", "Settings.bLeviathan=0"),
         metric="lumen_min_n", direction="treatment_lower", threshold=3.0,
         why="Separates predation from starvation as the binding constraint on the low-water "
             "mark of the population."),
    dict(name="drought selects exploration",
         when=("drought",), also=("epsilon",),
         arms=("C", "Settings.PatchRegenPerSec=1.8", "C", ""),
         metric="lumen_end_mean_epsilon", direction="treatment_higher", threshold=0.01,
         why="0.3x of the regen-6 baseline makes resources scarce and shifting; energy-gated "
             "reproduction should then favour the higher-epsilon genomes."),
    dict(name="adaptation continues under drought",
         when=("without offline retraining", "adaptation continuing", "keeps learning"),
         arms=("C", "Settings.PatchRegenPerSec=1.8", "C", ""),
         metric="lumen_greedy_changed_pct", direction="treatment_higher", threshold=2.0,
         why="The claim is about learning during the shock, not about surviving it: the share of "
             "state-action rows whose greedy action changed is what 'still adapting' means for a "
             "tabular contextual bandit."),
    dict(name="high-e crossover",
         when=("high-e", "env_effect", "plenty"),
         arms=("C", "Settings.PatchRegenPerSec=1.8", "C", ""),
         metric="lumen_end_mean_env_effect", direction="treatment_higher", threshold=0.01,
         why="The two halves of the crossover are the two arms: e pays for itself only where "
             "regrowth is the binding constraint, so end env_effect must separate drought "
             "from plenty."),
    dict(name="Tecton engineering",
         when=("trace", "engineering"),
         arms=("C", "", "C", "Settings.WeightInteraction=0"),
         metric="lumen_end_n", direction="treatment_higher", threshold=3.0,
         why="wI=0.10 is the only reason 'modify' is learnable for a gamma=0 bandit (caveat "
             "H-001), so zeroing it is the cleanest available no-engineering control."),
    dict(name="survivable drought dip",
         when=("drought",), also=("surviv", "dip", "population"),
         arms=("C", "Settings.PatchRegenPerSec=1.8", "C", ""),
         metric="lumen_min_n", direction="treatment_lower", threshold=3.0,
         why="A visible but survivable dip is a statement about the minimum, not the endpoint."),
    dict(name="inheritance fidelity",
         when=("inherit", "mutation", "sigma", "fidelity"),
         arms=("C", "Settings.MutationSigma=0.06", "C", ""),
         metric="inherit_alpha_corr", direction="treatment_lower", threshold=0.05,
         why="Doubling the mutation width should decouple parent and offspring alpha; the "
             "parent/child correlation is the direct readout."),
    dict(name="novelty bonus",
         when=("novelty", "exploration bonus", "explore leg", "river"),
         arms=("C", "Settings.WeightNovelty=0", "C", ""),
         metric="lumen_end_n", direction="treatment_higher", threshold=3.0,
         why="Turning the novelty reward off tests whether wN=0.2 buys exploration or just "
             "spends the Lumen energy budget on the dry valley rim. Read at the END of the run, "
             "not at the minimum: X-020 preregistered lumen_min_n and both arms returned exactly "
             "40.0, because the trough is reached in the opening seconds, before a novelty bonus "
             "can have spent anything (caveat H-025)."),
    dict(name="regen band robustness",
         when=("band", "stable", "regen-6"),
         arms=("C", "Settings.PatchRegenPerSec=4", "C", ""),
         metric="lumen_min_n", direction="treatment_lower", threshold=3.0,
         seeds=[1, 2, 3, 4, 5], duration=1800.0,
         why="The open question asks for seeds 1-5 at 1800 s, so the protocol asks for exactly "
             "that: a regen-4 arm against the regen-6 baseline."),
    dict(name="selection within a run",
         when=("selection", "turnover", "drift", "generation"),
         arms=("C", "", "N", ""),
         metric="lumen_end_mean_alpha", direction="treatment_higher", threshold=0.01,
         why="Neutral drift is the null this claim needs: same world, same turnover, no "
             "selection on the learning parameters."),
]

# Nothing matched: still a real contrast (C against neutral drift), never two identical arms.
DEFAULT_DESIGN = dict(name="learning and evolution vs neutral drift",
                      when=(), arms=("C", "", "N", ""),
                      metric="lumen_end_mean_alpha", direction="treatment_higher",
                      threshold=0.01,
                      why="No bench row fits the claim; the standing contrast at least "
                          "separates selection from drift.")


def _threshold_for(metric):
    """Band to use when a docketed crux replaces the bench row's own metric."""
    if any(k in metric for k in ("alpha", "epsilon", "corr", "slope", "env_effect", "frac")):
        return 0.01
    return 3.0


class MockLLM:
    """Profile-driven scripted scientist. context is passed via `ctx` on turn()."""

    def available(self):
        return True

    def turn(self, profile, kind, prompt, ctx=None):
        ctx = ctx or {}
        fn = getattr(self, "_" + kind)
        return fn(profile, ctx)

    # each evidence item in ctx["evidence"] is "E-001: stat=value — provenance"
    def _findings(self, profile, ctx):
        kws = profile.mock.get("agree_keywords", [])
        scored = sorted(ctx.get("evidence", []),
                        key=lambda e: (-_match(e, kws), e))
        picked = [e for e in scored if _match(e, kws)][:config.MAX_FINDINGS_PER_OBSERVER]
        finds = []
        for e in picked:
            eid, rest = e.split(":", 1)
            finds.append({
                "text": f"{profile.name} notes {rest.strip().split(' — ')[0]} and reads it "
                        f"through a {profile.domains[0] if profile.domains else 'general'} lens.",
                "evidence_ids": [eid.strip()]})
        return {"findings": finds}

    def _hypotheses(self, profile, ctx):
        # Ada (and observers) restate un-carded open questions as testable claims.
        pool = ctx.get("candidate_claims", [])
        props = []
        for claim, mech, scope in pool[:config.MAX_NEW_HYPOTHESES_PER_MEETING]:
            props.append({"claim": claim, "mechanism": mech, "scope": scope,
                          "evidence_ids": ctx.get("cited", [])[:1]})
        return {"proposals": props}

    def _stance(self, profile, ctx):
        claim = ctx.get("claim", "")
        ag = _match(claim, profile.mock.get("agree_keywords", []))
        dg = _match(claim, profile.mock.get("disagree_keywords", []))
        ev = ctx.get("evidence_ids", [])
        base = float(profile.mock.get("base_confidence", 0.6))
        if ag > dg and ev:
            return {"stance": "agree", "confidence": min(0.95, base + 0.05 * ag),
                    "reason": f"Consistent with my prior reading of the cited stats ({profile.priors[0]})",
                    "evidence_ids": ev[:2]}
        if dg > ag:
            return {"stance": "disagree", "confidence": min(0.9, base + 0.05 * dg),
                    "reason": f"prior: {profile.priors[0]}",
                    "evidence_ids": ev[:1]}
        # tie-break: temperament decides between cautious abstain and weak agree
        if ev and _h(profile.name, claim) % 3 == 0:
            return {"stance": "agree", "confidence": base * 0.8,
                    "reason": "Weak but consistent evidence.", "evidence_ids": ev[:1]}
        return {"stance": "abstain", "confidence": 0.3,
                "reason": "Insufficient evidence either way.", "evidence_ids": []}

    def _design(self, claim):
        """The bench row this claim calls for (see DESIGN_TEMPLATES)."""
        c = (claim or "").lower()
        for t in DESIGN_TEMPLATES:
            if not any(k in c for k in t["when"]):
                continue
            if t.get("also") and not any(k in c for k in t["also"]):
                continue
            return t
        return DEFAULT_DESIGN

    def _crux(self, profile, ctx):
        t = self._design(ctx.get("claim", ""))
        return {"observable": f"{t['metric']} under the intervention arm vs control",
                "measurable": True, "metric": t["metric"],
                "direction_if_claim_true": t["direction"]}

    def _protocol(self, profile, ctx):
        t = self._design(ctx.get("claim", ""))
        crux = ctx.get("crux", {}) or {}
        metric = crux.get("metric") or t["metric"]
        imode, iset, cmode, cset = t["arms"]
        seeds = list(t.get("seeds", [1, 2, 3]))
        return {"intervention_mode": imode, "intervention_set": iset,
                "control_mode": cmode, "control_set": cset,
                "seeds": seeds, "duration": t.get("duration", 900.0),
                "metric": metric,
                "threshold": t["threshold"] if metric == t["metric"] else _threshold_for(metric),
                "direction_if_claim_true": crux.get("direction_if_claim_true", t["direction"]),
                "rationale": f"{t['why']} [{t['name']}: {imode}/{iset or 'baseline'} vs "
                             f"{cmode}/{cset or 'baseline'}, {len(seeds)} seeds x 2 arms, "
                             f"{t.get('duration', 900.0):.0f}s]"}

    def _review(self, profile, ctx):
        proto = ctx.get("protocol", {})
        objections = []
        if len(proto.get("seeds", [])) < 2:
            objections.append("Fewer than 2 seeds: cannot separate effect from seed noise.")
        if proto.get("kind", "contrast") != "assessment" and \
           proto.get("intervention_mode") == proto.get("control_mode") and \
           proto.get("intervention_set") == proto.get("control_set"):
            objections.append("Intervention and control arms are identical.")
        if "modify" in ctx.get("claim", "").lower():
            objections.append("Caveat: any modify-learning claim must state the "
                              "WeightInteraction (wI=0.10) reward bias.")
        veto = bool(objections[:2] and "identical" in " ".join(objections))
        return {"approve": not veto, "objections": objections,
                "veto_reason": objections[0] if veto else ""}

    def _prediction(self, profile, ctx):
        claim = ctx.get("claim", "")
        direction = ctx.get("direction_if_claim_true", "treatment_higher")
        ag = _match(claim, profile.mock.get("agree_keywords", []))
        dg = _match(claim, profile.mock.get("disagree_keywords", []))
        base = float(profile.mock.get("base_confidence", 0.6))
        if ag > dg:
            return {"predicted": direction, "confidence": min(0.9, base + 0.05 * ag),
                    "reason": "The claim matches my priors; I expect it to hold."}
        if dg > ag:
            return {"predicted": "no_difference", "confidence": base,
                    "reason": "I expect the boring explanation: no separable effect."}
        other = "treatment_lower" if direction == "treatment_higher" else "treatment_higher"
        pick = [direction, "no_difference", other][_h(profile.name, claim) % 3]
        return {"predicted": pick, "confidence": 0.45,
                "reason": "No strong prior; registering a weak guess."}

    def _challenge(self, profile, ctx):
        return {"text": f"Strongest objection ({profile.name}, appointed dissenter): "
                        f"the effect could be a confound of shared seeds or the wI reward "
                        f"bias rather than '{ctx.get('claim', '')[:80]}'. A mode-N or "
                        f"knockout arm would separate them."}

    def _minutes(self, profile, ctx):
        return {"summary": ctx.get("draft", "Meeting held; see structured records.")}

    def _agenda(self, profile, ctx):
        qs = [q for q in ctx.get("open_questions", [])][:2]
        return {"priorities": qs or ["What governs population stability under drought?"],
                "focus_metric": "lumen_min_n",
                "rationale": "Populations first; the registry's open questions set the docket."}

    def _arbitration(self, profile, ctx):
        vr = (ctx.get("veto_reason") or "").lower()
        uphold = "identical" in vr or "missing control" in vr
        return {"uphold": uphold,
                "reason": ("Without a control the data is uninterpretable; veto stands."
                           if uphold else
                           "The design is imperfect but controlled; run it and let the data argue.")}


def make_llm(backend):
    if backend == "mock":
        return MockLLM()
    if backend == "claude":
        llm = ClaudeCLILLM()
        if not llm.available():
            raise SystemExit("`claude` CLI not found on PATH; install Claude Code "
                             "or run with --llm mock / --llm ollama.")
        return llm
    if backend == "openrouter":
        llm = OpenRouterLLM()
        if not llm.available():
            raise SystemExit("OPENROUTER_API_KEY not set; export it (and optionally "
                             "LAB_OPENROUTER_MODEL), or run with --llm mock/claude/ollama.")
        return llm
    llm = OllamaLLM()
    if not llm.available():
        raise SystemExit(
            f"Ollama not reachable at {config.OLLAMA_URL}. Install/start Ollama and "
            f"`ollama pull {config.OLLAMA_MODEL}`, or run with --llm mock.")
    return llm
