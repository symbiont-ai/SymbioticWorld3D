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


# --- deterministic mock ------------------------------------------------------

def _h(*parts):
    return int(hashlib.md5("|".join(str(p) for p in parts).encode()).hexdigest(), 16)


def _match(text, keywords):
    t = text.lower()
    return sum(1 for k in keywords if k.lower() in t)


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

    def _crux(self, profile, ctx):
        claim = ctx.get("claim", "").lower()
        metric, direction = "lumen_end_mean_alpha", "treatment_higher"
        if "epsilon" in claim:
            metric = "lumen_end_mean_epsilon"
        elif "drought" in claim and ("surviv" in claim or "dip" in claim or "population" in claim):
            metric, direction = "lumen_min_n", "treatment_lower"
        elif "selection" in claim or "drift" in claim or "turnover" in claim:
            metric = "lumen_end_mean_alpha"
        elif "env_effect" in claim or "high-e" in claim or "engineering" in claim or "trace" in claim:
            metric = "lumen_end_n" if "lumen" in claim else "trace_y_end_mean"
        elif "regen" in claim or "band" in claim or "stable" in claim:
            metric, direction = "lumen_min_n", "treatment_lower"
        return {"observable": f"{metric} under the intervention arm vs control",
                "measurable": True, "metric": metric,
                "direction_if_claim_true": direction}

    def _protocol(self, profile, ctx):
        crux = ctx.get("crux", {})
        if not crux.get("metric"):
            crux = self._crux(profile, ctx)   # no docketed crux: derive metric from the claim
        claim = ctx.get("claim", "").lower()
        inter_set, control_set, mode, control_mode = "", "", "C", "C"
        if "drought" in claim:
            inter_set = "Settings.PatchRegenPerSec=1.8"   # 0.3x of the regen-6 baseline, schedulable today
        elif "selection" in claim or "drift" in claim:
            control_mode = "N"
        elif "regen" in claim or "stable" in claim or "band" in claim:
            inter_set = "Settings.PatchRegenPerSec=4"
        return {"intervention_mode": mode, "intervention_set": inter_set,
                "control_mode": control_mode, "control_set": control_set,
                "seeds": [1, 2, 3], "duration": 900.0,
                "metric": crux.get("metric", "lumen_end_mean_alpha"),
                "threshold": 0.01 if "alpha" in crux.get("metric", "") or
                                     "epsilon" in crux.get("metric", "") else 3.0,
                "direction_if_claim_true": crux.get("direction_if_claim_true",
                                                    "treatment_higher"),
                "rationale": "Cheapest decisive contrast for the docketed crux; "
                             "3 seeds, control arm, preregistered metric."}

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


def make_llm(backend):
    if backend == "mock":
        return MockLLM()
    if backend == "claude":
        llm = ClaudeCLILLM()
        if not llm.available():
            raise SystemExit("`claude` CLI not found on PATH; install Claude Code "
                             "or run with --llm mock / --llm ollama.")
        return llm
    llm = OllamaLLM()
    if not llm.available():
        raise SystemExit(
            f"Ollama not reachable at {config.OLLAMA_URL}. Install/start Ollama and "
            f"`ollama pull {config.OLLAMA_MODEL}`, or run with --llm mock.")
    return llm
