#!/usr/bin/env bash
# CEF PoC 采集（DEC-006 M3-06）。
# 用法：collect_cef.sh <page: harness|ui> <out前缀> [ui index.html 路径]
# 前置：ui/shell-poc/cef/build/poc_cef/ 已由 CMake 构建产出（README 见构建步骤）。
# 产出：<前缀>-latency.json（harness）/ <前缀>-ui.json（ui）、<前缀>-memory.json
set -euo pipefail

PAGE="${1:?usage: collect_cef.sh <harness|ui> <out-prefix> [ui-index-html]}"
PREFIX="${2:?missing out prefix}"
UI_URL="${3:-}"

DIR="$(cd "$(dirname "$0")" && pwd)"
POC_BIN="$DIR/../cef/build/Release/poc_cef"
RESULTS="$DIR/../results"
mkdir -p "$RESULTS"

if [ ! -x "$POC_BIN" ]; then
  echo "缺少 $POC_BIN，先构建：cmake -B ../cef/build -S ../cef -DCEF_ROOT=<sdk> && cmake --build ../cef/build" >&2
  exit 2
fi

if [ "$PAGE" = "ui" ]; then
  if [ -z "$UI_URL" ]; then echo "--page=ui 需要 ui index.html 路径" >&2; exit 2; fi
  OUT_JSON="$PREFIX-ui.json"
  # CEF 走 http://mira.local scheme handler 供给本地资产（file:// 的模块资产
  # 被 CORS 拦截）；UI_URL 参数传 dist 目录，脚本内部映射 index.html。
  ASSET_ROOT="$(cd "$(dirname "$UI_URL")" && pwd)"
  PAGE_ARGS=(--page=ui --asset-root="$ASSET_ROOT")
  PAGE_URL="http://mira.local/index.html"
else
  OUT_JSON="$PREFIX-latency.json"
  PAGE_ARGS=()
  PAGE_URL=""
fi

timeout 180 "$POC_BIN" --url="${PAGE_URL:-$DIR/../harness/bridge-latency.html}" \
  --out="$OUT_JSON" "${PAGE_ARGS[@]}" &
ROOT_PID=$!

python3 "$DIR/memory_sampler.py" --pid "$ROOT_PID" --interval-ms 500 \
  --out "$PREFIX-memory.json" --label cef || true
set +e
wait "$ROOT_PID"
rc=$?
set -e

echo "cef page=$PAGE exit=$rc out=$OUT_JSON"
python3 - "$OUT_JSON" <<'EOF'
import json, sys
try:
    with open(sys.argv[1]) as f: d = json.load(f)
except FileNotFoundError:
    print("结果文件不存在（CEF 运行失败或超时）"); raise SystemExit(1)
print("meta:", json.dumps(d.get("meta"), ensure_ascii=False)[:200])
print("ui_check:", json.dumps(d.get("ui_check")))
print("window_embed:", json.dumps(d.get("window_embed")))
for label, s in sorted(d.get("series", {}).items()):
    print(f"series {label}: {json.dumps(s['stats'])}")
diag = d.get("diagnostics", {})
print("console_msgs:", len(diag.get("console", [])),
      "load_errors:", diag.get("load_errors"))
EOF
exit $rc
