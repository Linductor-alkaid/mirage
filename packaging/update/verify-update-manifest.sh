#!/bin/bash
# M5-11 (DEC-006 决策 5): verify a signed update manifest against a trusted
# ed25519 public key, then every listed file against its sha256.
# FAIL CLOSED: the FIRST invalid signature/digest exits non-zero; there is
# no per-file tolerance and no unsigned-manifest acceptance.
#
# Usage: verify-update-manifest.sh --manifest FILE --sig FILE --pubkey FILE
#        [--dir DIR]
#   --dir: directory holding the listed files (default: manifest's dir).
set -eu

MANIFEST=""
SIG=""
PUBKEY=""
DIR=""

while [ $# -gt 0 ]; do
    case "$1" in
    --manifest) MANIFEST="$2"; shift 2 ;;
    --sig) SIG="$2"; shift 2 ;;
    --pubkey) PUBKEY="$2"; shift 2 ;;
    --dir) DIR="$2"; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[ -f "$MANIFEST" ] || { echo "no manifest (fail closed)" >&2; exit 2; }
[ -f "$SIG" ] || { echo "no signature (fail closed)" >&2; exit 2; }
[ -f "$PUBKEY" ] || { echo "no trusted public key (fail closed)" >&2; exit 2; }
[ -n "$DIR" ] || DIR="$(dirname "$MANIFEST")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# 1. Signature over the canonical manifest bytes. Nothing is read past this
#    step until the signature validates.
openssl pkeyutl -verify -pubin -inkey "$PUBKEY" -rawin -sigfile "$SIG" \
    -in "$MANIFEST" > /dev/null 2>&1 \
    || { echo "FAIL: manifest signature invalid (fail closed)" >&2; exit 1; }
echo "manifest signature: OK"

# 2. Schema gate: the manifest must declare the expected schema before any
#    file entry is trusted.
grep -q '"schema": "mirage-update-manifest"' "$MANIFEST" \
    || { echo "FAIL: unknown manifest schema (fail closed)" >&2; exit 1; }

# 3. Every listed file must exist and match its sha256 exactly.
#    Parsing uses sed on the constrained generator shape (name/size/sha256
#    per line block); any malformed entry fails closed.
FAILURES=0
ENTRIES=$(sed -n 's/.*{ "name": "\([^"]*\)", "size": \([0-9]*\), "sha256": "\([0-9a-f]*\)" }.*/\1|\2|\3/p' "$MANIFEST")
[ -n "$ENTRIES" ] || { echo "FAIL: no file entries (fail closed)" >&2; exit 1; }
while IFS='|' read -r name size digest; do
    path="$DIR/$name"
    if [ ! -f "$path" ]; then
        echo "FAIL: missing file: $name" >&2
        FAILURES=$((FAILURES + 1))
        continue
    fi
    actual=$(sha256sum "$path" | awk '{print $1}')
    if [ "$actual" != "$digest" ]; then
        echo "FAIL: sha256 mismatch: $name" >&2
        FAILURES=$((FAILURES + 1))
        continue
    fi
    actual_size=$(stat -c '%s' "$path")
    if [ "$actual_size" != "$size" ]; then
        echo "FAIL: size mismatch: $name" >&2
        FAILURES=$((FAILURES + 1))
    fi
done <<ENTRIES
$ENTRIES
ENTRIES

if [ "$FAILURES" -gt 0 ]; then
    echo "FAIL: $FAILURES file(s) failed verification (fail closed)" >&2
    exit 1
fi
echo "all listed files verified (sha256 + size)"
exit 0
