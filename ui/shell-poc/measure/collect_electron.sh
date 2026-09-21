#!/usr/bin/env bash
# Electron PoC 采集（DEC-006 M3-06）。
# 用法：collect_electron.sh <page: harness|ui> <out前缀> [ui index.html 路径]
# 产出：<前缀>-latency.json（harness）/ <前缀>-ui.json（ui）、<前缀>-memory.json
set -euo pipefail

PAGE="${1:?usage: collect_electron.sh <harness|ui> <out-prefix> [ui-index-html]}"
PREFIX="${2:?missing out prefix}"
UI_URL="${3:-}"

DIR="$(cd "$(dirname "$0")" && pwd)"
ELECTRON_DIR="$DIR/../electron"
RESULTS="$DIR/../results"
mkdir -p "$RESULTS"

cd "$ELECTRON_DIR"
if [ ! -d node_modules/electron ]; then
  npm install --no-audit --no-fund
fi

if [ "$PAGE" = "ui" ]; then
  if [ -z "$UI_URL" ]; then echo "--page=ui 需要 ui index.html 路径" >&2; exit 2; fi
  OUT_JSON="$PREFIX-ui.json"
else
  OUT_JSON="$PREFIX-latency.json"
fi
ARGS=(--page="$PAGE" --out="$OUT_JSON")
if [ "$PAGE" = "ui" ]; then
  ARGS+=(--url="$UI_URL")
fi

timeout 180 npx electron . --disable-gpu --no-sandbox "${ARGS[@]}" &
ROOT_PID=$!

python3 "$DIR/memory_sampler.py" --pid "$ROOT_PID" --interval-ms 500 \
  --out "$PREFIX-memory.json" --label electron || true
wait "$ROOT_PID"
rc=$?

echo "electron page=$PAGE exit=$rc out=$OUT_JSON"
python3 - "$OUT_JSON" <<'EOF'
import json, sys
with open(sys.argv[1]) as f: d = json.load(f)
print("meta:", json.dumps(d.get("meta"), ensure_ascii=False)[:200])
print("ui_check:", json.dumps(d.get("ui_check")))
for label, s in sorted(d.get("series", {}).items()):
    print(f"series {label}: {json.dumps(s['stats'])}")
print("console_msgs:", len(d.get("diagnostics", {}).get("console", [])),
      "preload_errors:", len(d.get("diagnostics", {}).get("preload_error", [])),
      "render_gone:", d.get("diagnostics", {}).get("render_gone"))
EOF
