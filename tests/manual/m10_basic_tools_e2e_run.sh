#!/usr/bin/env bash
# M10-09 manual product E2E: starts the real mirage-service with the basic
# tools enabled against an isolated socket/state directory, drives one real
# harness turn through the configured model endpoint (default settings
# file), and asserts the actual on-disk effects of the four tools.
# Requires: debug build, a configured model profile, OS credential access.
set -euo pipefail

BUILD_DIR=${BUILD_DIR:-build/debug}
SERVICE="$BUILD_DIR/apps/mirage-service"
PROBE="$BUILD_DIR/tests/m10_basic_tools_e2e"
[[ -x "$SERVICE" && -x "$PROBE" ]] || { echo "build $BUILD_DIR first"; exit 2; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/m10-e2e.XXXXXX")
SOCK="$WORK/mirage.sock"
SERVICE_PID=""
cleanup() {
    [[ -n "$SERVICE_PID" ]] && kill "$SERVICE_PID" 2>/dev/null || true
    [[ -n "$SERVICE_PID" ]] && wait "$SERVICE_PID" 2>/dev/null || true
}
trap cleanup EXIT

SCENARIOS=${SCENARIOS:-123}
scenario_enabled() { [[ "$SCENARIOS" == *"$1"* ]]; }

printf 'meeting notes from the space project\nTODO: call alice about the launch window\nend of notes\n' > "$WORK/notes.txt"

"$SERVICE" --socket "$SOCK" --state-dir "$WORK/state" --no-recovery \
    --read-root "$WORK" --write-root "$WORK" --workspace "$WORK" \
    --enable-basic-tools --enable-exec \
    --perm filesystem.read=allow --perm filesystem.write=allow \
    --perm process.execute=allow \
    --confirm deny >>"$WORK/service.log" 2>&1 &
SERVICE_PID=$!

for _ in $(seq 1 150); do
    [[ -S "$SOCK" ]] && break
    sleep 0.2
done
[[ -S "$SOCK" ]] || { echo "service did not create the socket"; tail -20 "$WORK/service.log"; exit 1; }
echo "service up (pid $SERVICE_PID, socket $SOCK, workspace $WORK)"

if scenario_enabled 1; then
TASK='请在工作区依次完成并逐步汇报：1) 读取 notes.txt；2) 用 edit 把其中的 "TODO: call alice about the launch window" 原样替换为 "DONE: call alice about the launch window"；3) 用 write 新建文件 run.sh，内容恰好为两行：第一行 #!/bin/sh，第二行 echo run-ok；4) 用 exec 运行命令 sh run.sh，确认输出包含 run-ok；5) 再读取一次 notes.txt 确认修改已落盘。'

if "$PROBE" "$SOCK" "$TASK" 600; then
    echo "PROBE OK"
else
    echo "PROBE FAILED"
    tail -40 "$WORK/service.log"
    exit 1
fi

fi

if scenario_enabled 1; then
grep -q "DONE: call alice about the launch window" "$WORK/notes.txt" \
    || { echo "EFFECT FAILED: notes.txt was not edited"; cat "$WORK/notes.txt"; exit 1; }
if grep -q "TODO: call alice" "$WORK/notes.txt"; then
    echo "EFFECT FAILED: TODO line still present"; exit 1
fi
printf '#!/bin/sh\necho run-ok\n' > "$WORK/run.sh.expected"
cat "$WORK/run.sh" > "$WORK/run.sh.actual"
diff "$WORK/run.sh.expected" "$WORK/run.sh.actual" \
    || { echo "EFFECT FAILED: run.sh content mismatch"; cat "$WORK/run.sh"; exit 1; }
echo "EFFECTS OK (notes.txt edited, run.sh created with exact bytes)"
fi

# Scenario 2: product-level cancellation of a running exec.
if scenario_enabled 2; then
echo "--- scenario 2: cancel a running exec ---"
if "$PROBE" "$SOCK" '请用 exec 运行命令 sleep 60，等它结束后汇报。' 120 exec-start     | tee "$WORK/cancel.out"; then :; fi
grep -q "cancel on exec-start accepted: yes" "$WORK/cancel.out"     || { echo "CANCEL FAILED: cancel request not accepted"; exit 1; }
grep -q 'terminated\":\"cancelled' "$WORK/cancel.out"     || { echo "CANCEL FAILED: exec was not torn down cooperatively"; exit 1; }
if pgrep -x sleep >/dev/null; then
    echo "CANCEL FAILED: the sleep process survived the turn cancellation"
    pkill -f "sleep 60" || true
    exit 1
fi
echo "CANCEL OK (turn settled, exec torn down, no surviving process)"
fi

# Scenario 3: an external modification between turns must be detected; the
# model has to re-read and recover instead of overwriting unseen content.
if scenario_enabled 3; then
echo "--- scenario 3: external modification between turns ---"
printf 'stage one\nvalue: alpha\nstage three\n' > "$WORK/config.txt"
PROBE_OUT="$WORK/recover.log"
"$PROBE" "$SOCK" '用 read 工具（不要用 exec/cat）读取 config.txt 并简要汇报其内容。' 300 > "$PROBE_OUT"
SESSION_ID=$(sed -n 's/^session: //p' "$PROBE_OUT" | head -1)
[[ -n "$SESSION_ID" ]] || { echo "RECOVER FAILED: no session id"; cat "$PROBE_OUT"; exit 1; }
printf 'stage one\nvalue: alpha\nstage three\nan extra line\n' > "$WORK/config.txt"  # external append-only write
if ! "$PROBE" "$SOCK" 'config.txt 你在本会话已经读取过。不要重新读取，直接用 edit 把其中的 "value: alpha" 原样替换为 "value: gamma"。如果工具报错，再读取文件并重试，直到成功。' 300 "$((-1))" "$SESSION_ID" | tee -a "$PROBE_OUT"; then
    echo "RECOVER FAILED: second turn did not settle ok"
    exit 1
fi
grep -q "stale_file" "$PROBE_OUT"     || { echo "RECOVER FAILED: the stale observation was not detected"; cat "$PROBE_OUT"; exit 1; }
grep -q "value: gamma" "$WORK/config.txt"     || { echo "RECOVER FAILED: the edit did not land after recovery"; cat "$WORK/config.txt"; exit 1; }
grep -q "an extra line" "$WORK/config.txt"     || { echo "RECOVER FAILED: the externally added line was lost"; cat "$WORK/config.txt"; exit 1; }
echo "RECOVER OK (stale detected, model re-read, edit applied, external bytes intact)"
fi
echo "--- workspace ---"
ls -la "$WORK"
echo "--- service log tail ---"
tail -5 "$WORK/service.log"
