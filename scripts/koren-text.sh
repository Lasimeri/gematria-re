#!/bin/sh
# koren-text.sh - fetch the Koren edition Torah (the text of Witztum, Rips
# and Rosenberg 1994) and the first 78,064 letters of the Hebrew War and
# Peace from Brendan McKay's codes pages, and write the Torah in the stage K
# corpus format with the OSIS book ids of the WLC export.
#
# Usage: scripts/koren-text.sh RAWDIR OUT.txt MANIFEST
#
# Source: https://users.cecs.anu.edu.au/~bdm/dilugim/data/ (genesis.koren ..
# deuteronomy.koren, WNP.txt). The raw files are not committed. Format of a
# .koren line: book, then chapter and verse each with its digits reversed
# (chapter 21 is written 12, verse 10 is written 01), then the words in
# Michigan-Claremont transliteration; a long verse continues on further
# lines with the same three numbers, broken between words. Final forms are
# restored at word ends (kaf, mem, nun, pe, tsadi), as the WLC export has
# them.
set -eu
raw=$1
out=$2
manifest=$3
base=https://users.cecs.anu.edu.au/~bdm/dilugim/data
mkdir -p "$raw"
: > "$out"
: > "$manifest"
for pair in genesis:Gen exodus:Exod leviticus:Lev numbers:Num deuteronomy:Deut; do
    file=${pair%%:*}.koren
    osis=${pair#*:}
    [ -s "$raw/$file" ] || curl -sS -f -L -m 60 -o "$raw/$file" "$base/$file"
    LC_ALL=C awk -v osis="$osis" '
    BEGIN {
        split(") B G D H W Z X + Y K L M N S ( P C Q R $ T", mc, " ")
        split("א ב ג ד ה ו ז ח ט י כ ל מ נ ס ע פ צ ק ר ש ת", he, " ")
        for (i = 1; i <= 22; i++)
            map[mc[i]] = he[i]
        fin["K"] = "ך"; fin["M"] = "ם"; fin["N"] = "ן"; fin["P"] = "ף"; fin["C"] = "ץ"
    }
    function rev(s,   r, i) {
        r = ""
        for (i = length(s); i >= 1; i--)
            r = r substr(s, i, 1)
        return r
    }
    NF >= 4 {
        ref = osis " " rev($2) ":" rev($3)
        text = ""
        for (f = 4; f <= NF; f++) {
            n = length($f)
            word = ""
            for (i = 1; i <= n; i++) {
                ch = substr($f, i, 1)
                if (!(ch in map)) {
                    bad++
                    continue
                }
                word = word ((i == n && (ch in fin)) ? fin[ch] : map[ch])
            }
            text = text (text == "" ? "" : " ") word
        }
        if (ref == cur) {
            line = line " " text
        } else {
            if (cur != "")
                print cur "\t" line
            cur = ref
            line = text
        }
    }
    END {
        if (cur != "")
            print cur "\t" line
        if (bad)
            print osis ": " bad " characters outside the transliteration" > "/dev/stderr"
    }' "$raw/$file" >> "$out"
    printf '%s: raw sha256 %s  %s\n' "$osis" "$(sha256sum "$raw/$file" | cut -d' ' -f1)" "$base/$file" >> "$manifest"
done
[ -s "$raw/WNP.txt" ] || curl -sS -f -L -m 60 -o "$raw/WNP.txt" "$base/WNP.txt"
printf 'War and Peace (first 78,064 letters, Hebrew): raw sha256 %s  %s\n' "$(sha256sum "$raw/WNP.txt" | cut -d' ' -f1)" "$base/WNP.txt" >> "$manifest"
printf 'text sha256 %s  %s\n' "$(sha256sum "$out" | cut -d' ' -f1)" "$(basename "$out")" >> "$manifest"
printf 'bytes %s lines %s\n' "$(wc -c < "$out")" "$(wc -l < "$out")" >> "$manifest"
