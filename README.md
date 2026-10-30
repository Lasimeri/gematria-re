# gematria-re

Reverse-engineering gematria as a measurable system: a strict witness of
the Hebrew Bible, the classical methods implemented from cited
definitions, and a claim auditor that prices every equality in degrees of
freedom against the corpus itself. C17 ingest and core, C++23 statistics,
Rust TUI. Results are labelled by the preregistration they were frozen
under (`prereg/`); every statistics run verifies the corpus and the
preregistrations by sha256.

## Build and run

```
git clone --depth 1 https://github.com/openscriptures/morphhb.git data/morphhb   # pin: 3d15126
cmake -S . -B build -G Ninja && cmake --build build
build/gem-ingest data/morphhb/wlc out/wlc.gemc     # S0 witness, prints the Torah comparison
build/gem-selftest out/wlc.gemc                    # 16 methods vs cited values
build/gem-stat out/wlc.gemc claim יין סוד          # claim auditor (add "all 1" for 16 methods, K=+/-1)
build/gem-stat out/wlc.gemc r1 | r2 | r2b | r2x    # replications (r2x is exploratory)
cd tui && cargo build --release && target/release/gem-tui
```

TUI input: Hebrew, or Michigan-Claremont ASCII (`)BGDHWZX+YKLMNS(PCQR&$T`,
also `A` alef, `@` ayin); `A = B` audits a claim. Tab switches hechrachi /
all 16 methods, F2 the kolel policy.

## Measured (2026-10-30, Ryzen 7 5800X, 16 threads)

| Stage | Result |
|---|---|
| S0 ingest | 39 books, 23,213 verses, 305,507 words, 1,197,042 letters; 0 violations, 0 unknown code points; 0.09 s |
| Torah | 304,850 letters (Ben Asher 304,805: +45; third-party Leningrad 304,850: exact); vav +24, yod +34; tsadi -90 unresolved |
| Methods | 16, Wikipedia "Gematria" (Cordovero list); selftest 21/21 incl. Sanhedrin 38a, Yoma 20a |
| Claim yayin = sod | hechrachi, K=0: 82 of 39,614 types also match, p = 0.0021, rare (DoF 1); 16 methods, K=+/-1: p = 0.354, chance (DoF 48) |
| R1 (gamatria.app analogue) | pair collision 1 in 704 (692); 30 types share 46.5 % (45 %); median share 0.136 % (0.13 %) / 34.7 % (34.9 %) |
| R2 loose (Pichkhadze) | coverage 937 = source; 13 missing values identical |
| R2 strict | PREREG-001 rule 907 (not replicated); PREREG-002 rule (suffixes stripped) 867 and 56 missing values identical |
| R2 nulls (PREREG-002) | M1 927.90 +/- 5.54, M2 926.66 +/- 5.15 vs source 946.9 / 938.1: NOT MET; 0 of 1,300 replicates <= observed |
| Xeon Phi 7120A | not used: R1 + R2 take 0.11 s + 0.36 s on the host, the card takes 23 s to boot |

Out of scope by design: no decoding of persons, events or dates; no
prediction mode. A bare equality is never reported without its p and DoF.

## Design

```
╔══════════════════════════════════════════════════════════════════════════════════════════════╗
║ DESKTOP HOST, CPU: Ryzen 7 5800X, 16 threads, 31 GiB     ~/gematria-re, git local only       ║
║                                                                                              ║
║ ┌─ OSHB morphhb ───────────┐   ┌─ gem-ingest (C17) ───────┐   ┌─ out/wlc.gemc ───────────┐   ║
║ │ 39 OSIS books, 28 MB     │   │ strict OSIS, 0.09 s      │   │ GEMC v2, sha 46ce61ff    │   ║
║ │ WLC 4.20, git 3d15126    │──▶│ 0 violations, 0 unknown  │──▶│ 305,507 words            │   ║
║ │ read by explicit name    │   │ Torah 304,850 letters    │   │ 1,197,042 letters        │   ║
║ └──────────────────────────┘   └──────────────────────────┘   └──────────────────────────┘   ║
║                                                                             ▼                ║
║ ┌─ Xeon Phi 7120A 2f:00.0 ─┐   ┌─ libgem.a (C17) ─────────┐   ┌─ libgemstats.a (C++23) ──┐   ║
║ │ NOT USED: host does R2   │   │ 16 methods (Cordovero)   │   │ 39,615 types, 16 indexes │   ║
║ │ in 0.36 s, boot is 23 s  │   │ selftest 21/21, cited    │──▶│ claim auditor, R1, R2    │   ║
║ │ (ELS = first candidate)  │   │ Hebrew UTF-8 + corpus    │   │ sha256: corpus + preregs │   ║
║ └──────────────────────────┘   └──────────────────────────┘   └──────────────────────────┘   ║
║                                                                             ▼                ║
║ ┌─ Konsole (desktop) ──────┐   ┌─ gem-tui (Rust 2024) ────┐   ┌─ gemstat.h (C ABI) ──────┐   ║
║ │ redraw on keystroke only │   │ ratatui 0.30, 0 clippy   │   │ no exception crosses     │   ║
║ │ verdict green / red      │◀──│ Michigan-Claremont in    │◀──│ FFI = CLI to the digit   │   ║
║ │ Hebrew col. last (BiDi)  │   │ values, matches, p, DoF  │   │ 82 of 39,614: p 0.0021   │   ║
║ └──────────────────────────┘   └──────────────────────────┘   └──────────────────────────┘   ║
║                                                                                              ║
║ PREREG-001 f09c7ad7 (before any statistic)   PREREG-002 1cac4867 (before its nulls)          ║
║ R2 loose 937 = source, missing list EXACT; strict 907 (001) / 867 EXACT (002)                ║
║ nulls 927.9 / 926.7 vs source 946.9 / 938.1: NOT MET; 0 of 1,300 replicates <= observed      ║
╚══════════════════════════════════════════════════════════════════════════════════════════════╝
```
