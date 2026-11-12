#!/bin/sh
# fetch-sy.sh - fetch a Hebrew Sefer Yetzirah text from the Sefaria API and
# write it in the stage K corpus format: "<LABEL> <chapter>:<mishnah>" TAB
# text.
#
# Usage: scripts/fetch-sy.sh TITLE LABEL OUT.txt MANIFEST
#   scripts/fetch-sy.sh Sefer_Yetzirah SY data/sy/sefer-yetzirah-warsaw1884.txt data/sy/MANIFEST
#   scripts/fetch-sy.sh Sefer_Yetzirah_Gra_Version GRA data/sy/sefer-yetzirah-gra-warsaw1884.txt data/sy/MANIFEST.gra
#
# Source: https://www.sefaria.org/api/texts/TITLE.N (N = 1..6), field .he.
# The manifest records the version title, licence and source Sefaria reports
# for each chapter, and the sha256 of the result, so a later fetch can be
# compared against the pinned copy. No temporary files.
set -eu
title=$1
label=$2
out=$3
manifest=$4
: > "$out"
: > "$manifest"
for ch in 1 2 3 4 5 6; do
    url="https://www.sefaria.org/api/texts/$title.$ch?context=0&commentary=0&pad=0"
    json=$(curl -sS -f -m 40 "$url")
    printf '%s' "$json" | jq -r --arg ch "$ch" --arg label "$label" \
        '.he | to_entries[] | "\($label) \($ch):\(.key + 1)\t\(.value | gsub("<[^>]*>"; " ") | gsub("[\t\n\r]"; " "))"' >> "$out"
    printf '%s' "$json" | jq -r --arg ch "$ch" --arg url "$url" \
        '"chapter \($ch): \(.he | length) mishnayot | \(.heVersionTitle) | \(.heLicense) | \(.heVersionSource) | \($url)"' >> "$manifest"
done
printf 'sha256 %s  %s\n' "$(sha256sum "$out" | cut -d' ' -f1)" "$(basename "$out")" >> "$manifest"
printf 'bytes %s lines %s\n' "$(wc -c < "$out")" "$(wc -l < "$out")" >> "$manifest"
