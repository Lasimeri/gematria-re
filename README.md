# gematria-re

Reverse-engineering gematria as a measurable system:
- a strict witness of the Hebrew Bible;
- the classical methods, implemented from cited definitions;
- a claim auditor that prices every equality in degrees of freedom against
  the corpus itself;
- preregistered tests of the strongest claims made for gematria and for
  Bible codes.

C17 ingest and core, C++23 statistics, Rust TUI. The equidistant letter
sequence (ELS) kernel also runs on the Xeon Phi 7120A's 512-bit vector unit.

Every result carries the preregistration it was frozen under (`prereg/`).
Every statistics run verifies the corpus and all preregistrations by sha256.
Every reported run is stored in `results/`.

## Build and run

```
git init -q data/morphhb && git -C data/morphhb fetch -q --depth 1 \
    https://github.com/openscriptures/morphhb.git 3d15126fb1ef74867fc1434be1942e837932691f \
    && git -C data/morphhb checkout -q FETCH_HEAD                    # exact pin
cmake -S . -B build -G Ninja && cmake --build build
build/gem-ingest data/morphhb/wlc out/wlc.gemc     # S0 witness, prints the Torah comparison
build/gem-selftest out/wlc.gemc                    # 16 methods vs cited values
build/gem-pairs-test                               # PREREG-003 pair counter vs brute force
build/gem-stat out/wlc.gemc claim יין סוד          # claim auditor (add "all 1" for 16 methods, K=+/-1)
build/gem-stat out/wlc.gemc r1 | r2 | r2b | cluster   # PREREG-001 / -002 / -003
build/gem-stat out/wlc.gemc r2x | base             # exploratory diagnostics
build/gem-els out/wlc.gemc rambsel | design | bench   # PREREG-004 parts A, B, kernel A/B
scripts/manifest.sh data/morphhb/wlc out/wlc.gemc | diff - prereg/wlc.manifest   # L0 hashes
cd tui && cargo build --release && target/release/gem-tui
```

### Card (Xeon Phi 7120A)

```
bash card/build.sh els-card card/els_card.c        # Intel-Phi-LLVM -march=knc + phi-isa-audit
build/gem-els out/wlc.gemc export out/card/seed.bin 1000 gen   # 80 KB: text + seeds
phi up; phi put build-card/els-card /tmp/els-card; phi put out/card/seed.bin /tmp/seed.bin
phi run /tmp/els-card /tmp/seed.bin 236 V 1 4      # 59 cores x 4 threads, vector kernel V
```

### TUI input

- Type Hebrew, or Michigan-Claremont ASCII (`)BGDHWZX+YKLMNS(PCQR&$T`; `A`
  also gives alef and `@` ayin).
- `A = B` audits a claim.
- Tab switches between hechrachi and all 16 methods; F2 toggles the kolel
  policy.

## Measured (2026-10-30, Ryzen 7 5800X 16 threads, Xeon Phi 7120A)

Timings are single runs unless a spread is given.

| Stage | Result |
|---|---|
| S0 ingest | 39 books, 23,213 verses, 305,507 words, 1,197,042 letters; 0 violations, 0 unknown code points; 0.09 s. |
| Torah | 304,850 letters: +45 against Ben Asher's 304,805, exactly the third-party Leningrad count 304,850. Vav +24, yod +34; tsadi -90 is unresolved. |
| Methods | 16, from Wikipedia "Gematria" (Cordovero list). Selftest 21/21, including Sanhedrin 38a and Yoma 20a. |
| Claim yayin = sod | Hechrachi, K=0: 82 of 39,614 types also match, p = 0.0021, rare (1 DoF). 16 methods, K=+/-1: p = 0.354, chance (48 DoF). |
| Auditor base rate (exploratory) | p is A's match share and does not depend on B: yayin = ken scores exactly like yayin = sod. Share of types with p < 0.01: 100.0 % under hechrachi K=0, 0.0 % under 16 methods K=+/-1. The verdict prices the declared method set, not the pair. |
| R1 (gamatria.app analogue) | Pair collision 1 in 704 (source 692). 30 types share a value 46.5 % (45 %). Median match share 0.136 % (0.13 %), and 34.7 % with 16 methods (34.9 %). |
| R2 (Pichkhadze) | Loose coverage 937, with all 13 missing values identical. PREREG-001's strict rule gives 907 (not replicated). PREREG-002's rule (suffixes stripped) gives 867, with all 56 missing values identical. Nulls 927.9 / 926.7 against 946.9 / 938.1: NOT MET. 0 of 1,300 replicates fall at or below the observed value. |
| PREREG-003 clustering | Do equal-valued, different words share verses beyond chance? Primary (Pentateuch, verse, K=0): S 1,110 vs exact E 1,109.7, R 1.000. Against 1,000 random value tables, p = 0.4755: no design signal. Secondary Q2: 0 of 7 significant. The K=+/-1 excess also appears under random tables (generic clustering). |
| PREREG-004 A (Rambsel/Jeffrey) | ישועשמי at skip -20 from Isa 53:10 is present in WLC. P(at least one) in Isa 52:13-53:12 = 1 in 3,414; claimed 1 in 5e16. Over all of Isaiah (exploratory, the claim's own scope): it occurs once, and P(at least one) = 1 in 38 (same model, arithmetic). ישוע alone: observed 1, expected 0.661, p 0.605. |
| PREREG-004 B (own words as ELS) | Genesis T 39,472 vs permutation null 39,819.3 +/- 207.2, z -1.68, p 0.957. Psalms control: z -0.64. No excess. |
| ELS kernels (host, 1 thread, d 2..200) | Plain 4.918 s; 5-stream 0.0299 s; rolling key 0.0231 s; rolling + 8 KB prefilter **0.0187 s**. All counts identical. |
| Xeon Phi 7120A, kernel V (vpermd register filter) | Exact counts on 1,001 texts, reproducing PREREG-004 B to the last digit. Same split over d: 11.68 vs 17.9 ms/text (1.53x). Batch mode with on-card permutation generation: 10.62 s per 1,001 texts. The best scalar host kernel (H, `-march=native`, one text per thread) takes 11.8 / 11.7 / 11.8 s, so the card wins the kernel **1.11x**. End-to-end with the card booted: about 11.3 s (single run) vs 11.9 s, **1.05x = a tie**. Cold boot adds 23.3 s (the host wins). An AVX2 host version of the lane kernel was not built: the card beats the best scalar host kernel, nothing more. |

Out of scope by design: no decoding of persons, events or dates, and no
prediction mode. A bare equality is never reported without its p and DoF
price.

## Design

```
╔══════════════════════════════════════════════════════════════════════════════════════════════╗
║ DESKTOP HOST, CPU: Ryzen 7 5800X 16t, 31 GiB + Xeon Phi 7120A (PCIe)   ~/gematria-re         ║
║                                                                                              ║
║ ┌─ OSHB morphhb ───────────┐   ┌─ gem-ingest (C17) ───────┐   ┌─ out/wlc.gemc ───────────┐   ║
║ │ 39 OSIS books, 28 MB     │   │ strict OSIS, 0.09 s      │   │ GEMC v2, sha 46ce61ff    │   ║
║ │ WLC 4.20, git 3d15126    │──▶│ 0 violations, 0 unknown  │──▶│ 305,507 words            │   ║
║ │ read by explicit name    │   │ Torah 304,850 letters    │   │ 1,197,042 letters        │   ║
║ └──────────────────────────┘   └──────────────────────────┘   └──────────────────────────┘   ║
║                                                                             ▼                ║
║ ┌─ Xeon Phi 7120A (KNC) ───┐   ┌─ gem-els export gen ─────┐   ┌─ libgemstats + libgem ───┐   ║
║ │ kernel V: vpermd filter  │   │ 80 KB: text + seeds      │   │ 16 methods, 21/21 cited  │   ║
║ │ 1,001 texts in 10.62 s   │◀──│ phi put 0.05 s           │◀──│ auditor, R1, R2, cluster │   ║
║ │ host scalar 11.8 s: 1.11x│   │ card makes 1,000 perms   │   │ ELS kernels P S R H      │   ║
║ └──────────────────────────┘   └──────────────────────────┘   └──────────────────────────┘   ║
║                                                                             ▼                ║
║ ┌─ Konsole (desktop) ──────┐   ┌─ gem-tui (Rust 2024) ────┐   ┌─ gemstat.h (C ABI) ──────┐   ║
║ │ redraw on keystroke only │   │ ratatui 0.30, 0 clippy   │   │ no exception crosses     │   ║
║ │ verdict + base rate      │◀──│ Michigan-Claremont in    │◀──│ FFI = CLI to the digit   │   ║
║ │ Hebrew col. last (BiDi)  │   │ values, matches, p, DoF  │   │ 82 of 39,614: p 0.0021   │   ║
║ └──────────────────────────┘   └──────────────────────────┘   └──────────────────────────┘   ║
║                                                                                              ║
║ PREREG-001 f09c7ad7, -002 1cac4867, -003 048f905c, -004 0589ea9d: each frozen before its run ║
║ R2 loose 937 EXACT; strict 867 EXACT (002); nulls 927.9 / 926.7 vs 946.9 / 938.1: NOT MET    ║
║ PREREG-003 do equal values cluster? primary R 1.000, p 0.4755: no design signal              ║
║ PREREG-004 ELS: Rambsel phrase 1 in 3,414 (passage), 1 in 38 (Isaiah); claim 5e16; z -1.68   ║
║ auditor base rate: 100.0 % of types 'rare' (hechrachi K=0), 0.0 % (16 methods +/-1)          ║
║ card vs best scalar host: exact counts, kernel 1.11x, end-to-end 1.05x = tie (booted)        ║
╚══════════════════════════════════════════════════════════════════════════════════════════════╝
```
