"""Read-only contract probe; does not execute the upstream parser tests."""

import json
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[3]
mira = root / "third_party/mira"
header = (mira / "include/mira/model_sse.hpp").read_text()
match = re.search(r"struct UnvalidatedModelPreview final\s*\{(.*?)\n\};", header, re.S)
assert match, "preview contract changed; re-audit the public API"
fields = match.group(1)
assert "std::string text;" in fields
assert not re.search(r"thinking|reasoning|kind", fields), "new API requires re-audit"
api = (mira / "docs/api/model-agent-loop.md").read_text()
assert "SSE 思考不进入文本预览" in api
thinking = "ModelThinkingPreviewSink" in header
if thinking:
    assert "UnvalidatedModelThinkingPreview" in header
    for path in ("model_gateway.hpp", "model_provider.hpp"):
        assert "thinking_preview_sink" in (mira / "include/mira" / path).read_text()
chat = (mira / "tests/m3/m3_sse_test.cpp").read_text()
messages = (mira / "tests/m3/m3_anthropic_test.cpp").read_text()
assert "Pure reasoning increments are invisible to the live preview" in chat
assert "Reasoning never becomes answer preview" in messages
lock = json.loads((root / "dependencies.lock.json").read_text())
print(json.dumps({
    "feedback": "MIRA-20261014-004",
    "scope": "static public API/document/test assertions only; no parser execution",
    "preview_fields": fields.strip().splitlines(),
    "live_thinking_preview": "independent opt-in sink exposed" if thinking else "not exposed",
    "lock_commit": next(dep["commit"] for dep in lock["dependencies"] if dep["name"] == "mira"),
}, ensure_ascii=False, indent=2))
