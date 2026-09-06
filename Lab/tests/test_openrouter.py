#!/usr/bin/env python3
"""Offline test of the OpenRouter backend (no network, no key needed).

Stubs urllib at the module boundary and asserts: per-profile model routing,
Authorization header, schema-in-prompt, JSON extraction from a fenced reply,
response_format dropped after a 4xx, and a parse failure after 3 attempts.

  python3 Lab/tests/test_openrouter.py
"""
import io
import json
import sys
import urllib.error
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from Lab import llm as llm_mod  # noqa: E402
from Lab.profiles import Profile  # noqa: E402


def _profile(model=""):
    d = {"name": "Vesper", "role": "Lumen ethologist", "temperature": 0.9,
         "priors": ["behavior first"], "speaking_style": "terse"}
    if model:
        d["model"] = model
    return Profile(d)


class _Resp(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


def _install(replies):
    """Replace urlopen with a stub yielding canned replies; records requests."""
    calls = []

    def fake_urlopen(req, timeout=None):
        calls.append(req)
        r = replies[min(len(calls) - 1, len(replies) - 1)]
        if isinstance(r, Exception):
            raise r
        return _Resp(json.dumps(r).encode())

    llm_mod.urllib.request.urlopen = fake_urlopen
    return calls


def _chat(content):
    return {"choices": [{"message": {"content": content}}]}


def main():
    real_urlopen = llm_mod.urllib.request.urlopen
    try:
        backend = llm_mod.OpenRouterLLM(model="default/model", key="k-test")
        assert backend.available()

        # 1. Clean reply: parsed; request carries model, auth, schema, temperature.
        calls = _install([_chat('{"findings": []}')])
        out = backend.turn(_profile(), "findings", "ROUND 1")
        assert out == {"findings": []}
        body = json.loads(calls[0].data)
        assert body["model"] == "default/model"
        assert body["temperature"] == 0.9
        assert body["response_format"]["json_schema"]["name"] == "findings"
        assert "JSON schema" in body["messages"][1]["content"]
        assert calls[0].get_header("Authorization") == "Bearer k-test"

        # 2. Per-scientist model overrides the default.
        calls = _install([_chat('{"summary": "ok"}')])
        backend.turn(_profile(model="anthropic/claude-haiku-4.5"), "minutes", "x")
        assert json.loads(calls[0].data)["model"] == "anthropic/claude-haiku-4.5"

        # 3. Fenced/prose-wrapped JSON still extracts.
        _install([_chat('```json\n{"stance": "agree", "confidence": 0.7, '
                        '"reason": "r", "evidence_ids": []}\n```')])
        out = backend.turn(_profile(), "stance", "x")
        assert out["stance"] == "agree"

        # 4. 4xx on response_format: dropped, retried, succeeds.
        err = urllib.error.HTTPError("u", 400, "bad response_format", {}, io.BytesIO(b""))
        calls = _install([err, _chat('{"summary": "ok"}')])
        out = backend.turn(_profile(), "minutes", "x")
        assert out == {"summary": "ok"}
        assert "response_format" not in json.loads(calls[1].data)

        # 5. Persistent garbage: RuntimeError after 3 attempts.
        calls = _install([_chat("no json here")])
        try:
            backend.turn(_profile(), "minutes", "x")
            raise AssertionError("expected RuntimeError")
        except RuntimeError as ex:
            assert "no parseable JSON" in str(ex)
        assert len(calls) == 3
        assert "response_format" not in json.loads(calls[2].data)
    finally:
        llm_mod.urllib.request.urlopen = real_urlopen
    print("test_openrouter: all assertions passed")


if __name__ == "__main__":
    main()
