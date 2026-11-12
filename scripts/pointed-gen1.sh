#!/bin/sh
# pointed-gen1.sh - Genesis 1 as the OSHB source prints it (vowel points,
# cantillation accents, maqaf, paseq, sof pasuq), in the stage K corpus
# format: the real-text input for the tokenizer's mark stripping and
# punctuation handling (tests/kab_test.cpp compares it verse by verse against
# the letters-only export of the same chapter).
#
# Usage: scripts/pointed-gen1.sh data/morphhb/wlc/Gen.xml > OUT.txt
#
# Takes every line of chapter 1 that starts with a <w> element, removes the
# tags (a line can hold several words joined by a maqaf <seg>, and the final
# word's line holds the sof pasuq <seg>: their text is kept) and removes the
# OSHB morpheme separator '/', which is markup, not text. Genesis 1 has no
# ketiv/qere; paragraph markers and notes sit on their own lines and are
# skipped. If a later OSHB revision changes that, the comparison fails loudly.
set -eu
awk '
/<verse osisID="Gen\.1\./ {
    match($0, /Gen\.1\.[0-9]+/)
    split(substr($0, RSTART, RLENGTH), id, ".")
    line = "Gen 1:" id[3] "\t"
    sep = ""
    inside = 1
    next
}
inside && /^[ \t]*<w / {
    t = $0
    gsub(/<[^>]*>/, "", t)
    gsub("/", "", t)
    sub(/^[ \t]+/, "", t)
    line = line sep t
    sep = " "
    next
}
inside && /<\/verse>/ {
    print line
    inside = 0
}
' "$1"
