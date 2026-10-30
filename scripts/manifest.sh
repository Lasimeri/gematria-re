#!/usr/bin/env bash
# manifest.sh - hash the witness inputs and the normalized corpus (layer L0).
#
# Usage: scripts/manifest.sh WLC_DIR CORPUS.gemc > MANIFEST
# Records the morphhb commit, the sha256 of each of the 39 book files (by
# explicit name, never a glob: wlc/ also holds VerseMap.xml) and of the
# corpus file, so every downstream number names the exact bytes it used.
set -euo pipefail

wlc=$1
corpus=$2
books=(Gen Exod Lev Num Deut Josh Judg 1Sam 2Sam 1Kgs 2Kgs Isa Jer Ezek
       Hos Joel Amos Obad Jonah Mic Nah Hab Zeph Hag Zech Mal
       Ps Prov Job Song Ruth Lam Eccl Esth Dan Ezra Neh 1Chr 2Chr)

echo "morphhb_commit $(git -C "$wlc/.." rev-parse HEAD)"
for b in "${books[@]}"; do
    sha256sum "$wlc/$b.xml" | awk -v b="$b" '{ print "input " b " " $1 }'
done
sha256sum "$corpus" | awk '{ print "corpus " $1 }'
