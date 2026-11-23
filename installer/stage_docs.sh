#!/usr/bin/env bash
# Writes the documents every installer ships into <dest>: Readme.txt (the README's Install and Using it sections, as plain text) and
# licenses/ (the third-party notices in THIRD_PARTY_NOTICES.md and the project's own LICENSE). Called by the macOS, Windows and Linux
# packaging steps.
#
# Usage: installer/stage_docs.sh <dest> [version]

set -euo pipefail

DEST="${1:?Usage: stage_docs.sh <dest> [version]}"
VERSION="${2:-}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" # the repository root

mkdir -p "$DEST/licenses"

# Install and Using it, from their headings up to the next other one, with Markdown emphasis and links flattened.
{
    echo "YREV57${VERSION:+ $VERSION}"
    echo
    awk '/^## (Install|Using it)/{on=1; print; next} /^## /{on=0} on' "$REPO/README.md" |
        perl -0pe 's/\*\*(.+?)\*\*/$1/gs; s/\*(.+?)\*/$1/gs; s/\[([^]]+)\]\([^)]+\)/$1/g'
    echo
    echo "YREV57 is licensed under the GNU AGPL v3: see licenses/YREV57-LICENSE.txt. Source: https://github.com/tehguitarist/YREV57"
    echo
    echo "YREV57 is an independent product, inspired by the Yamaha REV7 and REV5. It is not affiliated with, endorsed by or"
    echo "sponsored by Yamaha Corporation. Yamaha, REV7 and REV5 are trademarks of Yamaha Corporation, used here only to"
    echo "describe the hardware that inspired this plug-in."
    echo
    echo "Third-party software: see the licenses folder next to this file."
} > "$DEST/Readme.txt"

cp "$REPO/THIRD_PARTY_NOTICES.md" "$DEST/licenses/THIRD_PARTY_NOTICES.txt"
cp "$REPO/LICENSE" "$DEST/licenses/YREV57-LICENSE.txt"
echo "Staged docs in $DEST"
