#!/bin/sh
# fetch-mam.sh - fetch one book of "Miqra according to the Masorah" (MAM,
# CC-BY-SA, an edition in the Aleppo Codex tradition) from the Sefaria API,
# one JSON file per chapter, sequentially.
#
# Usage: scripts/fetch-mam.sh BOOK CHAPTERS OUTDIR
#   e.g. scripts/fetch-mam.sh Genesis 50 data/mam/raw
#
# The raw files are not committed (the text is CC-BY-SA; this repository
# publishes only what is derived from it). scripts/mam-text.sh turns them
# into the stage K corpus format and writes the manifest with the hashes.
set -eu
book=$1
chapters=$2
out=$3
mkdir -p "$out"
ch=1
while [ "$ch" -le "$chapters" ]; do
    url="https://www.sefaria.org/api/texts/$book.$ch?context=0&commentary=0&pad=0&vhe=Miqra_according_to_the_Masorah"
    curl -sS -f -m 60 --retry 2 -o "$out/$book.$ch.json" "$url"
    title=$(jq -r '.heVersionTitle' "$out/$book.$ch.json")
    [ "$title" = "Miqra according to the Masorah" ] || { echo "$book $ch: unexpected version: $title" >&2; exit 1; }
    ch=$((ch + 1))
done
echo "$book: $chapters chapters, $(cat "$out/$book".*.json | wc -c) bytes"
