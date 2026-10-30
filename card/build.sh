#!/usr/bin/env bash
# card/build.sh NAME SOURCE... : build one Xeon Phi 7120A (KNC) program into build-card/NAME.
# Recipe of the Phi Cortex (tools/knc-one.sh): Intel-Phi-LLVM clang -march=knc (the 512-bit vector unit),
# static link by the 7120A stack's knc-cc, then phi-isa-audit rejects any instruction the card lacks.
# Extra compile flags through DEFS.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out="$here/build-card"
mkdir -p "$out/obj"
flags=(-O2 -std=gnu11 -Wall -Wextra -Werror)
VCC="${PHI_LLVM_BIN:-$HOME/.cache/intel-phi-llvm-build/llvm/bin}/clang"
SYS="${PHI_SYSROOT_7120A:-$HOME/.cache/intel-phi-7120a-build/toolchain/sysroot}"
tree="${PHI_TREE:-$HOME/Intel Phi 7120A}"
# shellcheck disable=SC1091
. "$tree/toolchain/env.sh"
audit="$tree/host/target/debug/phi-isa-audit"
name=$1
shift
objs=()
for s in "$@"; do
	o="$out/obj/$(basename "${s%.c}")-${name}.o"
	# shellcheck disable=SC2086
	"$VCC" --target=x86_64-unknown-linux-musl -march=knc --sysroot="$SYS" "${flags[@]}" ${DEFS:-} -fno-jump-tables \
		-falign-loops=1 -fPIE -c -o "$o" "$here/$s"
	objs+=("$o")
done
knc-cc "${flags[@]}" -static -o "$out/$name" "${objs[@]}" -lm -lpthread
printf '%s: ' "$name"
"$audit" "$out/$name" | tail -1
