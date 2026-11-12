#!/bin/sh
# mam-text.sh - turn the raw MAM chapters fetched by scripts/fetch-mam.sh
# into the stage K corpus format ("<book> <chapter>:<verse>" TAB text, with
# the OSIS book ids of the WLC export) and write the manifest.
#
# Usage: scripts/mam-text.sh RAWDIR OUT.txt MANIFEST
#
# What is kept is the written text (ketiv). Removed, in this order:
#   editorial footnotes       <i class="footnote">...</i> and their markers
#   qere readings             <span class="mam-kq-q">[...]</span>
#   parashah marks, inverted  <span class="mam-spi-...">...</span>
#   nuns
#   every remaining tag       the tag only: its text stays, so a large or
#                             small letter stays inside its word
#   entities                  replaced by a space
# The ketiv keeps its parentheses and the paseq its character; the stage K
# tokenizer treats both as separators.
set -eu
raw=$1
out=$2
manifest=$3
: > "$out"
: > "$manifest"
for pair in Genesis:Gen:50 Exodus:Exod:40 Leviticus:Lev:27 Numbers:Num:36 Deuteronomy:Deut:34; do
    book=${pair%%:*}
    rest=${pair#*:}
    osis=${rest%%:*}
    chapters=${rest#*:}
    ch=1
    while [ "$ch" -le "$chapters" ]; do
        jq -r --arg osis "$osis" --arg ch "$ch" '.he | to_entries[] | "\($osis) \($ch):\(.key + 1)\t" + (.value
            | gsub("<i class=\"footnote\">.*?</i>"; "")
            | gsub("<sup class=\"footnote-marker\">.*?</sup>"; "")
            | gsub("<span class=\"mam-kq-q\">.*?</span>"; "")
            | gsub("<span class=\"mam-spi-[a-z]+\">.*?</span>"; "")
            | gsub("<[^>]*>"; "")
            | gsub("&[a-z]+;"; " ")
            | gsub("[\t\n\r]"; " "))' "$raw/$book.$ch.json" >> "$out"
        ch=$((ch + 1))
    done
    printf '%s: %s chapters, raw sha256 %s\n' "$book" "$chapters" \
        "$(ch=1; while [ "$ch" -le "$chapters" ]; do cat "$raw/$book.$ch.json"; ch=$((ch + 1)); done | sha256sum | cut -d' ' -f1)" >> "$manifest"
done
printf 'version: Miqra according to the Masorah | CC-BY-SA | Sefaria API, parameter vhe=Miqra_according_to_the_Masorah\n' >> "$manifest"
printf 'text sha256 %s  %s\n' "$(sha256sum "$out" | cut -d' ' -f1)" "$(basename "$out")" >> "$manifest"
printf 'bytes %s lines %s\n' "$(wc -c < "$out")" "$(wc -l < "$out")" >> "$manifest"
