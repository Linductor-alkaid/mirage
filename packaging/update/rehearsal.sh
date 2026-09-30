#!/bin/bash
# M5-11 更新通道演练 (DEC-006 决策 5): full local rehearsal of the update
# channel primitives against throwaway artifacts and an ephemeral test key:
#   1. sign a manifest over artifacts            → verify OK
#   2. tamper one artifact                       → verify FAILS (fail closed)
#   3. restore the artifact                      → verify OK
#   4. tamper the manifest bytes                 → verify FAILS (fail closed)
#   5. rollback: re-serve the previous good manifest + artifacts → verify OK
# All state lives in a temp directory; the rehearsal never touches release
# key material (ephemeral ed25519 rehearsal key only).
set -eu
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
SCRIPTS=/home/linductor/mirage/packaging/update

echo "[1] artifacts + manifest + sign + verify"
printf 'mirage-installer-body-v7\n' > mirage-installer.exe
printf 'mirage-update-notes-v7\n' > update-notes.txt
"$SCRIPTS/make-update-manifest.sh" --out . --test-key --version 0.1.0 \
    --file mirage-installer.exe=mirage-installer.exe \
    --file update-notes.txt=update-notes.txt \
    --pubkey-out trust.asc > step1.log
"$SCRIPTS/verify-update-manifest.sh" \
    --manifest update-manifest.json --sig update-manifest.json.sig \
    --pubkey trust.asc
echo "[1] verify OK"
cp update-manifest.json update-manifest.json.good
cp trust.asc trust.asc.good

echo "[2] tamper an artifact → verify must FAIL"
printf 'tampered!!\n' > mirage-installer.exe
if "$SCRIPTS/verify-update-manifest.sh" \
    --manifest update-manifest.json --sig update-manifest.json.sig \
    --pubkey trust.asc 2> /dev/null; then
    echo "REHEARSAL FAILED: tampered artifact accepted" >&2
    exit 1
fi
echo "[2] tampered artifact rejected (fail closed) ✓"

echo "[3] restore the artifact → verify OK again"
printf 'mirage-installer-body-v7\n' > mirage-installer.exe
"$SCRIPTS/verify-update-manifest.sh" \
    --manifest update-manifest.json --sig update-manifest.json.sig \
    --pubkey trust.asc > /dev/null
echo "[3] verify OK ✓"

echo "[4] tamper the manifest → verify must FAIL"
sed -i 's/"schema_version": 1/"schema_version": 9/' update-manifest.json
if "$SCRIPTS/verify-update-manifest.sh" \
    --manifest update-manifest.json --sig update-manifest.json.sig \
    --pubkey trust.asc 2> /dev/null; then
    echo "REHEARSAL FAILED: tampered manifest accepted" >&2
    exit 1
fi
echo "[4] tampered manifest rejected (fail closed) ✓"

echo "[5] rollback: the distributor re-serves the last good manifest + artifacts"
# Rollback = the distributor re-serves the last good manifest and artifacts
# (the client's key material is unchanged); the client re-verifies and
# converges on the last good release.
cp update-manifest.json.good update-manifest.json
"$SCRIPTS/verify-update-manifest.sh" \
    --manifest update-manifest.json --sig update-manifest.json.sig \
    --pubkey trust.asc > /dev/null
echo "[5] rollback converged on the last good release ✓"

echo "M5-11 update channel rehearsal: ALL steps passed"
exit 0
