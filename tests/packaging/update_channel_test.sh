#!/bin/bash
# M5-11 update channel verification (independent verification pass, DEC-006
# decision 5): exercises the producer/verifier pair end to end against
# throwaway artifacts and an ephemeral ed25519 key — the positive round trip
# plus the fail-closed matrix (tampered artifact, missing artifact, untrusted
# public key, wrong schema under a VALID signature, empty file list under a
# VALID signature, and the producer's refusal to emit an unsigned manifest).
#
# POSIX-only by nature (bash + openssl + sha256sum + stat on the producer
# side); registered in tests/CMakeLists.txt where those tools exist.
#
# Direct invocation:
#   tests/packaging/update_channel_test.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPDATE_DIR="$(cd "$SCRIPT_DIR/../../packaging/update" && pwd)"

for tool in openssl sha256sum stat bash; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "update_channel_test: required tool missing: $tool (loud failure, never a skip)" >&2
        exit 1
    fi
done
for script in "$UPDATE_DIR/make-update-manifest.sh" "$UPDATE_DIR/verify-update-manifest.sh"; do
    if [ ! -f "$script" ]; then
        echo "update_channel_test: script under test missing: $script" >&2
        exit 1
    fi
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

CHECKS=0
FAILURES=0

check() { # check NAME RESULT(0=pass)
    CHECKS=$((CHECKS + 1))
    if [ "$2" -ne 0 ]; then
        FAILURES=$((FAILURES + 1))
        echo "CHECK failed: $1" >&2
    fi
}

must_fail() { # must_fail NAME -- cmd... : pass when the command fails
    local name="$1"
    shift
    [ "${1:-}" = "--" ] && shift
    CHECKS=$((CHECKS + 1))
    if "$@" > /dev/null 2>&1; then
        FAILURES=$((FAILURES + 1))
        echo "CHECK failed: $name (fail-closed path accepted the input)" >&2
    fi
}

verify() { # verify --manifest F --sig F --pubkey F
    bash "$UPDATE_DIR/verify-update-manifest.sh" "$@"
}

# --- fixtures ------------------------------------------------------------

printf 'mirage-installer-body-v7\n' > mirage-installer.exe
printf 'mirage-update-notes-v7\n' > notes.txt
bash "$UPDATE_DIR/make-update-manifest.sh" --out . --test-key --version 1.2.3 \
    --file mirage-installer.exe=mirage-installer.exe \
    --file notes.txt=notes.txt \
    --pubkey-out trust.asc > make.log 2>&1
check "producer emits a signed manifest with the test key" $?
check "manifest file exists" \
    $( [ -f update-manifest.json ] && echo 0 || echo 1 )
check "signature file exists" \
    $( [ -f update-manifest.json.sig ] && echo 0 || echo 1 )
check "test key is marked NOT-RELEASE" \
    $( grep -q "NOT-RELEASE" make.log && echo 0 || echo 1 )

# --- 1. positive round trip ---------------------------------------------

verify --manifest update-manifest.json --sig update-manifest.json.sig \
    --pubkey trust.asc > /dev/null 2>&1
check "positive round trip verifies" $?

# --- 2. tampered artifact is rejected ------------------------------------

printf 'tampered!!\n' > mirage-installer.exe
must_fail "tampered artifact rejected" -- verify \
    --manifest update-manifest.json --sig update-manifest.json.sig --pubkey trust.asc
printf 'mirage-installer-body-v7\n' > mirage-installer.exe

# --- 3. missing artifact file is rejected --------------------------------

mv mirage-installer.exe mirage-installer.exe.held
must_fail "missing artifact rejected" -- verify \
    --manifest update-manifest.json --sig update-manifest.json.sig --pubkey trust.asc
mv mirage-installer.exe.held mirage-installer.exe

# --- 4. untrusted public key is rejected ---------------------------------

openssl genpkey -algorithm ed25519 -out untrusted.key 2> /dev/null
openssl pkey -in untrusted.key -pubout -out untrusted.pub 2> /dev/null
must_fail "untrusted public key rejected" -- verify \
    --manifest update-manifest.json --sig update-manifest.json.sig --pubkey untrusted.pub

# --- 5. wrong schema under a VALID signature is rejected ------------------

sed 's/mirage-update-manifest/mirage-other-manifest/' update-manifest.json > wrong-schema.json
openssl pkeyutl -sign -inkey mirage-update-ed25519-NOT-RELEASE.key -rawin \
    -in wrong-schema.json -out wrong-schema.json.sig 2> /dev/null
# Verify the fixture under the TRUSTED public key (trust.asc): proving the
# signature is valid is what attributes the later rejection to the schema
# gate. (The producer's private key file is intentionally not read here —
# `-pubin` against a private key is rejected by OpenSSL 3.0 on the CI
# runners while 3.5+ tolerates it.)
check "wrong-schema fixture signed (signature gate passes it on purpose)" \
    $( openssl pkeyutl -verify -pubin -inkey trust.asc \
           -rawin -sigfile wrong-schema.json.sig -in wrong-schema.json > /dev/null 2>&1 \
           && echo 0 || echo 1 )
must_fail "wrong schema rejected under a valid signature" -- verify \
    --manifest wrong-schema.json --sig wrong-schema.json.sig --pubkey trust.asc

# --- 6. empty file list under a VALID signature is rejected ---------------

printf '{\n  "schema": "mirage-update-manifest",\n  "schema_version": 1,\n' \
    > empty-files.json
printf '  "timestamp": "1970-01-01T00:00:00Z",\n  "files": [\n  ]\n}\n' >> empty-files.json
openssl pkeyutl -sign -inkey mirage-update-ed25519-NOT-RELEASE.key -rawin \
    -in empty-files.json -out empty-files.json.sig 2> /dev/null
must_fail "empty file list rejected under a valid signature" -- verify \
    --manifest empty-files.json --sig empty-files.json.sig --pubkey trust.asc

# --- 7. producer fail closed: no key material, no --test-key --------------

must_fail "producer refuses an unsigned manifest" -- bash "$UPDATE_DIR/make-update-manifest.sh" \
    --out "$WORK/no-key" --file mirage-installer.exe=mirage-installer.exe
check "refused producer left no manifest behind" \
    $( [ ! -f "$WORK/no-key/update-manifest.json" ] && echo 0 || echo 1 )

# --- summary --------------------------------------------------------------

echo "[update_channel_test] $CHECKS checks, $FAILURES failures"
[ "$FAILURES" -eq 0 ]
