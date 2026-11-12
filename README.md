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
Stage K (`gem-kab`) is a second, self-contained implementation in pure C++:
hardcoded letter tables, a UTF-8 corpus loader, offline vectors and the
shared-value graph, with the C core as its oracle.

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
build/gem-stat out/wlc.gemc order                  # PREREG-005 letter-order test
build/gem-stat out/wlc.gemc r2x | r2s | base       # exploratory diagnostics
build/gem-els out/wlc.gemc rambsel | design | bench   # PREREG-004 parts A, B, kernel A/B
scripts/manifest.sh data/morphhb/wlc out/wlc.gemc | diff - prereg/wlc.manifest   # L0 hashes
cd tui && cargo build --release && target/release/gem-tui
```

### Stage K (pure C++: tables, corpora, vectors, shared-value graph)

```
build/gem-export out/wlc.gemc out/kab/wlc.txt      # the witness as UTF-8, one verse per line
sh scripts/fetch-sy.sh Sefer_Yetzirah SY data/sy/sefer-yetzirah-warsaw1884.txt data/sy/MANIFEST   # Sefaria; compare the sha256
sh scripts/fetch-sy.sh Sefer_Yetzirah_Gra_Version GRA data/sy/sefer-yetzirah-gra-warsaw1884.txt data/sy/MANIFEST.gra
sh scripts/pointed-gen1.sh data/morphhb/wlc/Gen.xml > out/kab/gen1-pointed.txt
build/gem-kab-test out/kab/wlc.txt out/kab/gen1-pointed.txt     # C core as oracle, 22 checks
K="build/gem-kab WLC=out/kab/wlc.txt SY=data/sy/sefer-yetzirah-warsaw1884.txt"
$K index                       # unordered_map<int, vector<string>> per system, words and verses
$K value hechrachi 70          # the words and verses with one value
$K vectors out/kab/wlc-sy.kvec # offline float vectors (22 letter counts + 4 values), hash-checked
$K near יין 10                 # nearest words by cosine
$K graph                       # CSR graph per system, census of pairs by agreement mask
$K verify                      # census by grouping against brute force over every pair
$K pair יין סוד                # one equality: agreement mask, bucket sizes, base rate
$K dump out/kab/dump ref/attested.tsv   # matches to read by hand against the last corpus
$K bench 5                     # each kernel against the plain form it replaces

# second witness of the Torah (MAM, Aleppo tradition; the raw text is CC-BY-SA and is not committed)
for b in Genesis:50 Exodus:40 Leviticus:27 Numbers:36 Deuteronomy:34; do sh scripts/fetch-mam.sh ${b%:*} ${b#*:} data/mam/raw; done
sh scripts/mam-text.sh data/mam/raw data/mam/mam-torah.txt data/mam/MANIFEST    # compare the sha256
build/gem-kab WLC=out/kab/wlc.txt MAM=data/mam/mam-torah.txt witness out/kab/witness-sites.tsv
```

Corpus format: one verse per line, `reference<TAB>text`; the last corpus on
the command line is the reference text of `dump`. `ref/attested.tsv` lists
equalities already attested in the literature (word, word, source).

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
- `A = B` audits a claim; `A = 318` audits a number claim, priced by how
  many other words reach the number.
- Under the verdict, the stage K line gives the four values of A and B, the
  systems they agree in, and the base rate of that much agreement.
- Tab switches between hechrachi and all 16 methods; F2 toggles the kolel
  policy.

## Measured (2026-10-30, Ryzen 7 5800X 16 threads, Xeon Phi 7120A)

Timings are single runs unless a spread is given.

| Stage | Result |
|---|---|
| S0 ingest | 39 books, 23,213 verses, 305,507 words, 1,197,042 letters; 0 violations, 0 unknown code points; 0.09 s. |
| Torah | 304,850 letters: +45 against Ben Asher's 304,805, exactly the third-party Leningrad count 304,850. Vav +24, yod +34; tsadi -90 against the reference table (settled by the second witness, stage K below: the table is the outlier). |
| Methods | 16, from Wikipedia "Gematria" (Cordovero list). Selftest 21/21, including Sanhedrin 38a and Yoma 20a. |
| Claim yayin = sod | Hechrachi, K=0: 82 of 39,614 types also match, p = 0.0021, rare (1 DoF). 16 methods, K=+/-1: p = 0.354, chance (48 DoF). |
| Auditor base rate (exploratory) | p is A's match share and does not depend on B: yayin = ken scores exactly like yayin = sod. Share of types with p < 0.01: 100.0 % under hechrachi K=0, 0.0 % under 16 methods K=+/-1. The verdict prices the declared method set, not the pair. |
| R1 (gamatria.app analogue) | Pair collision 1 in 704 (source 692). 30 types share a value 46.5 % (45 %). Median match share 0.136 % (0.13 %), and 34.7 % with 16 methods (34.9 %). |
| R2 (Pichkhadze) | Loose coverage 937, with all 13 missing values identical. PREREG-001's strict rule gives 907 (not replicated). PREREG-002's rule (suffixes stripped) gives 867, with all 56 missing values identical. Nulls 927.9 / 926.7 against 946.9 / 938.1: NOT MET. 0 of 1,300 replicates fall at or below the observed value. |
| R2 null gap, explained (exploratory, `results/exploratory-r2s.txt`) | The source's code (Zenodo 10.5281/zenodo.22939702, `code/nullmodel.py`) permutes only the letters of the strict forms and fits its Markov chain on them; PREREG-001 and -002 defined both nulls over all Pentateuch letters, whose prefix and function-word letters pull the sums down. Run with the source's pool: M1 947.21 +/- 4.90 (source 946.91 +/- 4.87), z -16.4 (source -16.4); M2 937.85 +/- 4.91 (source 938.15 +/- 5.07). The preregistered verdict stays NOT MET; its cause is the pool. Residue: 59,569 strict forms and 212,098 letters here against 59,633 and 212,359 there (64 forms, 0.11 %), with identical coverage 867. |
| PREREG-003 clustering | Do equal-valued, different words share verses beyond chance? Primary (Pentateuch, verse, K=0): S 1,110 vs exact E 1,109.7, R 1.000. Against 1,000 random value tables, p = 0.4755: no design signal. Secondary Q2: 0 of 7 significant. The K=+/-1 excess also appears under random tables (generic clustering); its Q1 p of 0.0010 in three cells is the floor of a 1,000-replicate test (1/1001), a bound and not a measured value. |
| PREREG-004 A (Rambsel/Jeffrey) | ישועשמי at skip -20 from Isa 53:10 is present in WLC. P(at least one) in Isa 52:13-53:12 = 1 in 3,414; claimed 1 in 5e16. Over all of Isaiah (exploratory, the claim's own scope): it occurs once, and P(at least one) = 1 in 38 (same model, arithmetic). ישוע alone: observed 1, expected 0.661, p 0.605. |
| PREREG-004 B (own words as ELS) | Genesis T 39,472 vs permutation null 39,819.3 +/- 207.2, z -1.68, p 0.957. Psalms control: z -0.64. No excess. |
| ELS kernels (host, 1 thread, d 2..200) | Plain 4.918 s; 5-stream 0.0299 s; rolling key 0.0231 s; rolling + 8 KB prefilter **0.0187 s**. All counts identical. |
| Xeon Phi 7120A, kernel V (vpermd register filter) | Exact counts on 1,001 texts, reproducing PREREG-004 B to the last digit. Same split over d: 11.68 vs 17.9 ms/text (1.53x). Batch mode with on-card permutation generation: 10.62 s per 1,001 texts. The best scalar host kernel (H, `-march=native`, one text per thread) takes 11.8 / 11.7 / 11.8 s, so the card wins the kernel **1.11x**. End-to-end with the card booted: about 11.3 s (single run) vs 11.9 s, **1.05x = a tie**. Cold boot adds 23.3 s (the host wins). An AVX2 host version of the lane kernel was not built: the card beats the best scalar host kernel, nothing more. |

### Stage K (2026-11-12, Ryzen 7 5800X, one thread)

Exploratory and descriptive: base rates, no p-values, no preregistration.
Corpora: WLC (39,615 word types) and Sefer Yetzirah as Sefaria serves it
("Sefer Yetzirah, Warsaw 1884", Public Domain; which recension that edition
prints is not verified here). Runs: `results/exploratory-kab-*`,
`results/kab-test.txt`.

| Stage | Result |
|---|---|
| K1 tables | 27 `wchar_t`-keyed rows x 4 systems (hechrachi, gadol, katan, siduri), hardcoded. A `static_assert` regenerates every row from the definitions; a corrupted row does not compile. C core as oracle: 0 of 108 cells differ. |
| Structure of the systems | Proved per letter at compile time: hechrachi = katan x 10^d and siduri = katan + 9d (d = 0, 1, 2 for units, tens, hundreds). So siduri = katan + 9 x (sum of d) for every word, hechrachi = katan mod 9, and gadol differs from hechrachi only on the five finals. |
| K2 loader | The UTF-8 export of the witness loads to the frozen counts: 23,213 verses, 305,507 words, 1,197,042 letters, 39,615 types, in 22.6 ms. 0 of 39,615 x 4 values and 0 of 39,615 x 22 letter counts differ from the C core. Pointed Genesis 1 from the OSHB source: 1,742 marks dropped, 104 Hebrew punctuation marks split words, 0 of 31 verses differ. |
| Sefer Yetzirah | 48 mishnayot, 1,786 words, 6,738 letters, 755 word types, 297 of them absent from WLC. sha256 574881ba... (`data/sy/MANIFEST`). |
| Sefer Yetzirah, Gra version (`results/exploratory-kab-dump-gra.txt`) | The second recension Sefaria serves (same Warsaw 1884 print, Public Domain): 62 mishnayot, 2,015 words, 7,565 letters, 730 word types; 596 types shared with the first text, 159 only there, 134 only here. sha256 5b66548e... (`data/sy/MANIFEST.gra`). Inside it 467 pairs of different words agree in hechrachi (1 in 569.8), 64 in all four. 336 hechrachi pairs occur inside both recensions (of 524 and 467): the candidates that do not depend on the choice of recension. |
| K2 value indexes (39,912 types) | Equal-valued pairs of word types: hechrachi 1 in 703.7, gadol 1 in 1,026.9, katan 1 in 22.7, siduri 1 in 64.0. Verses: the largest hechrachi bucket (72 verses at 895) is one sentence repeated verbatim. |
| K3 vectors | 39,912 x 32 floats, unit length within 9.9e-08, 5,108,736 bytes, payload and lexicon hashed (a file from other corpora is refused). The nearest words by cosine are spelling neighbours and share no value: cosine and value equality are different relations. |
| K4 pair census | Among 796,427,143 pairs of types with different letter multisets, 103,310 agree in all four systems (1 in 7,709.1); four independent systems with the same single rates would give 0.706 pairs (arithmetic). Given agreement in hechrachi, gadol agrees in 52.4 %, katan in 44.3 %, siduri in 30.0 % of pairs (unconditional: 0.094 %, 4.39 %, 1.56 %). Census by grouping (20.5 ms) against brute force over 796,463,916 pairs (1.22 s): 0 of 33 counts differ. As information (bits = -log2 of the share of pairs): hechrachi alone 9.51 bits; gadol, katan and siduri on top add 0.93 + 1.19 + 1.29 = 3.41 bits, where independent systems would add 20.57. |
| K5 dump | Attested yayin = sod agrees in hechrachi only (1 in 727.3 pairs agree that much; 85 types carry 70); Amalek = safek agrees in 3 of 4 systems (1 in 3,160.5). Inside Sefer Yetzirah: 524 pairs of different words agree in hechrachi (1 in 543.2), 70 of them in all four. The head of the all-four list is single-letter arithmetic (alef-bet = gimel, 1 + 2 = 3): for the letters 1-9 the four systems are one system. Every pair therefore carries a kind, and "mixed" pairs are listed first: of the 103,310 all-four pairs 99,119 are mixed, 4,141 units-only, 50 single-letter; inside Sefer Yetzirah 464 mixed, 13 units-only, 47 single-letter. |
| Second witness of the Torah (`results/exploratory-kab-witness.txt`, sites in `results/exploratory-kab-witness-sites.tsv`) | "Miqra according to the Masorah" (MAM, Aleppo Codex tradition, via Sefaria) against WLC (Leningrad Codex), written text only, aligned by chapter: 304,801 against 304,850 letters, delta -49. 132 sites in 84 of 187 chapters: 125 plene/defective (78 of -1, 33 of +1, 12 of 0, 2 of -2), 5 word divisions, and one chapter boundary (three words close Num 25:19 in WLC and open Num 26:1 in MAM). Every letter difference is a vav (-24) or a yod (-25): no other consonant differs anywhere in the Torah. Verses 5,846 against 5,853 (Decalogue versification and that boundary). Per book MAM is 1, 2, 0, 1, 0 letters below the traditional 78,064 / 63,529 / 44,790 / 63,530 / 54,892. The stage G1 anomaly "tsadi -90" is settled: both witnesses count 3,962 tsadi, the reference table says 4,052, so the outlier is the table. |
| PREREG-005 letter order (frozen before its code; `results/prereg005-order.txt`) | Is the traditional letter order unusual in the four-system agreement it produces on the 39,615 WLC types? Against 1,000 random letter orders: T (pairs agreeing in all four) 102,005 vs 137,323.5 +/- 45,698.9 (60,177 to 456,130), p = 0.4416; T_H (hechrachi) 1,078,341 vs 1,286,791.8 +/- 296,374.8, p = 0.5255; T / T_H 0.0946 vs 0.1065, p = 0.6813. No signal. Gate met: both traditional counts equal the stage K census of the same lexicon. The expectation stated in the preregistration (above the median) was wrong in direction: 22.1 % of random orders give as few or fewer pairs. |
| Kernels against their plain forms (5 interleaved rounds, ratio of the fastest rounds) | One packed add per letter 1.19x; SWAR agreement mask 1.08x; CSR counting sort 9.63x against `unordered_map<int, vector<string>>`; AVX2+FMA cosine scan 7.39x. Results identical in every pair; the ratios of medians moved between runs (1.09x to 1.44x for the packed add), the replacement was never the slower one. |

Out of scope by design: no decoding of persons, events or dates, and no
prediction mode. A bare equality is never reported without its p and DoF
price; stage K reports a base rate where no test was preregistered.

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
║ │ redraw on keystroke only │   │ std + termios, 0 clippy  │   │ no exception crosses     │   ║
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

### Stage K

```
╔══════════════════════════════════════════════════════════════════════════════════════════════╗
║ DESKTOP HOST, CPU: Ryzen 7 5800X, one thread       ~/gematria-re stage K, pure C++23         ║
║                                                                                              ║
║ ┌─ UTF-8 corpora ──────────┐    ┌─ gem::kab::Corpus ───────┐    ┌─ tables.hpp ─────────────┐ ║
║ │ WLC text 2,926,720 B     │    │ strict UTF-8, 22.6 ms    │    │ 27 wchar_t rows x 4      │ ║
║ │ (gem-export wlc.gemc)    │───▶│ 305,507 + 1,786 words    │◀───│ static_assert on rows    │ ║
║ │ Sefer Yetzirah 15,949 B  │    │ 39,912 types: packed key │    │ siduri = katan + 9d      │ ║
║ │ Sefaria, sha 574881ba    │    │ + 110-bit letter set     │    │ agree_swar 1.08x plain   │ ║
║ └──────────────────────────┘    └─────────────┬────────────┘    └──────────────────────────┘ ║
║               ┌───────────────────────────────┼───────────────────────────────┐              ║
║               ▼                               ▼                               ▼              ║
║ ┌─ value indexes ──────────┐    ┌─ ValueGraph + census ────┐    ┌─ Vectors (.kvec) ────────┐ ║
║ │ unordered_map<int,       │    │ CSR x 4 in 0.49 ms       │    │ 39,912 x 32 f32, 5.1 MB  │ ║
║ │ vector<string>> x 4      │    │ 49,383,640 edges         │    │ unit norm, err 9.9e-08   │ ║
║ │ words + 23,261 verses    │    │ 796,463,916 pairs        │    │ FNV-1a payload+lexicon   │ ║
║ │ hechrachi 1 in 703.7     │    │ brute 1.22 s: 0 differ   │    │ scan_avx2 7.39x plain    │ ║
║ └──────────────────────────┘    └─────────────┬────────────┘    └─────────────┬────────────┘ ║
║                                               ▼                               ▼              ║
║ ┌─ gem-kab dump: hash equalities, unordered_map<uint64_t, vector<uint32_t>> ───────────────┐ ║
║ │ all four agree, letters differ: 103,310 pairs = 1 in 7,709.1 (0.706 if independent)      │ ║
║ │ inside Sefer Yetzirah (755 types): 524 hechrachi pairs = 1 in 543.2; 70 in all four      │ ║
║ │ attested yayin = sod: hechrachi only, 1 in 727.3; Amalek = safek: 3 of 4, 1 in 3,160.5   │ ║
║ │ out/kab/dump/matches-reference.tsv (524 rows), matches-all4.tsv (103,310 rows, 9.8 MB)   │ ║
║ └──────────────────────────────────────────────────────────────────────────────────────────┘ ║
║                                                                                              ║
║ gem-kab-test: libgem (C core) as oracle, 22 of 22; 39,615 x 4 values equal; pointed Gen 1 ok ║
║ bench, fastest of 5 rounds: sums 1.19x, agree 1.08x, CSR 9.63x, cosine 7.39x vs plain forms  ║
║ exploratory: base rates only, no p-values; every run stored in results/exploratory-kab-*     ║
╚══════════════════════════════════════════════════════════════════════════════════════════════╝
```
