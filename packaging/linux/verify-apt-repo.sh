#!/bin/bash
# M5-11: verify a Mirage apt repository signature chain against a trusted
# public key (FAIL CLOSED). Exit 0 only when InRelease's signature validates
# AND the detached Release.gpg matches AND the Release file is intact.
#
# Usage: verify-apt-repo.sh --repo DIR --key FILE
#   --key: the PUBLIC key exported by the release key holder
#     (gpg --armor --export <release-key-id> > mirage-apt.asc).
set -eu

REPO=""
KEY=""

while [ $# -gt 0 ]; do
    case "$1" in
    --repo) REPO="$2"; shift 2 ;;
    --key) KEY="$2"; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[ -f "$KEY" ] || { echo "no key: --key FILE is required (fail closed)" >&2; exit 2; }
[ -f "$REPO/dists/stable/InRelease" ] || { echo "no InRelease under $REPO/dists/stable (fail closed)" >&2; exit 2; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# A scratch GNUPGHOME imports the trusted public key; gpgv then verifies
# with exactly this trust material — nothing else counts.
export GNUPGHOME="$TMP/gnupg"
mkdir -p -m 700 "$GNUPGHOME"
gpg --batch --quiet --import "$KEY"

gpgv --keyring "$GNUPGHOME/pubring.kbx" "$REPO/dists/stable/InRelease" \
    || { echo "FAIL: InRelease signature invalid (fail closed)" >&2; exit 1; }
gpgv --keyring "$GNUPGHOME/pubring.kbx" "$REPO/dists/stable/Release.gpg" \
     "$REPO/dists/stable/Release" \
    || { echo "FAIL: Release.gpg signature invalid (fail closed)" >&2; exit 1; }

echo "apt repository signature chain verified (InRelease + Release.gpg)"
exit 0
