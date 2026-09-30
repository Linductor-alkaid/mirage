#!/bin/bash
# M5-11 (DEC-006 决策 5): build the SIGNED update manifest for the Windows
# in-app updater (M5-12 consumes it; this script is the release-side
# producer and the local 演练 vehicle).
#
# Manifest: JSON with schema/version/timestamp and one entry per file
# (name, size, sha256). The canonical manifest bytes are signed with
# ed25519 via OpenSSL; the signature lands in <manifest>.sig.
#
# Key isolation: the RELEASE signing key lives on the release machine only.
# For local rehearsal pass --test-key: an ephemeral ed25519 keypair is
# generated inside the output directory (private key clearly marked
# NOT-RELEASE; never shipped). FAIL CLOSED: without a key and --test-key
# the script refuses to produce an unsigned manifest.
#
# Usage:
#   make-update-manifest.sh --out DIR --file NAME=PATH [--file NAME=PATH ...]
#                           [--version V] [--key FILE] [--pubkey-out FILE]
#                           [--test-key]
set -eu

OUT=""
VERSION=""
KEY=""
PUBKEY_OUT=""
TEST_KEY=0
FILES=()

while [ $# -gt 0 ]; do
    case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --key) KEY="$2"; shift 2 ;;
    --pubkey-out) PUBKEY_OUT="$2"; shift 2 ;;
    --test-key) TEST_KEY=1; shift ;;
    --file) FILES+=("$2"); shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[ -n "$OUT" ] && [ "${#FILES[@]}" -gt 0 ] || {
    echo "usage: $0 --out DIR --file NAME=PATH [--file ...] [--version V] [--key FILE] [--pubkey-out FILE] [--test-key]" >&2
    exit 2
}
for kv in "${FILES[@]}"; do
    [ -f "${kv#*=}" ] || { echo "not a file: ${kv#*=}" >&2; exit 2; }
done

mkdir -p "$OUT"

SIGN_KEY="$KEY"
if [ "$TEST_KEY" -eq 1 ]; then
    SIGN_KEY="$OUT/mirage-update-ed25519-NOT-RELEASE.key"
    if [ ! -f "$SIGN_KEY" ]; then
        openssl genpkey -algorithm ed25519 -out "$SIGN_KEY" 2>/dev/null
        chmod 600 "$SIGN_KEY"
        echo "test key: $SIGN_KEY (local rehearsal, NOT the release key)"
    fi
    if [ -n "$PUBKEY_OUT" ]; then
        openssl pkey -in "$SIGN_KEY" -pubout -out "$PUBKEY_OUT" 2>/dev/null
    fi
fi
if [ -z "$SIGN_KEY" ]; then
    echo "refusing to produce an UNSIGNED manifest: pass --test-key for local" \
        "rehearsal or --key <ed25519 private key>" >&2
    exit 1
fi

MANIFEST="$OUT/update-manifest.json"
{
    echo '{'
    echo '  "schema": "mirage-update-manifest",'
    echo '  "schema_version": 1,'
    if [ -n "$VERSION" ]; then
        echo "  \"version\": \"$VERSION\","
    fi
    echo "  \"timestamp\": \"$(date -u '+%Y-%m-%dT%H:%M:%SZ')\","
    echo '  "files": ['
    first=1
    for kv in "${FILES[@]}"; do
        name="${kv%%=*}"
        path="${kv#*=}"
        size=$(stat -c '%s' "$path")
        digest=$(sha256sum "$path" | awk '{print $1}')
        [ $first -eq 1 ] || echo '    },'
        first=0
        printf '    { "name": "%s", "size": %s, "sha256": "%s" }' "$name" "$size" "$digest"
    done
    echo ''
    echo '  ]'
    echo '}'
} > "$MANIFEST"

# Sign the CANONICAL manifest bytes.
openssl pkeyutl -sign -inkey "$SIGN_KEY" -rawin -in "$MANIFEST" \
    -out "$MANIFEST.sig" 2>/dev/null \
    || { echo "manifest signing failed" >&2; exit 1; }

echo "manifest: $MANIFEST"
echo "signature: $MANIFEST.sig"
if [ -n "$PUBKEY_OUT" ]; then
    echo "public key: $PUBKEY_OUT"
fi
exit 0
