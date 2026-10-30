# PREREG-001: claim auditor and first replications

Frozen 2026-10-30, before any statistic on this corpus was computed. Its
sha256 is recorded in `prereg/PREREG-001.sha256` and printed by every tool
that reports a statistic under it. A change to anything below is a new
preregistration (PREREG-002), never an edit of this file.

## Witness

- WLC 4.20 through OSHB morphhb, commit `3d15126fb1ef74867fc1434be1942e837932691f`.
- Ketiv only: letters of `<w>` outside `<note>`; parser contract in
  `src/ingest/osis.c`.
- Corpus `out/wlc.gemc`, GEMC v2, sha256
  `46ce61ff38c678231f7283b13085a5278bee631ea6b40db62ec2737eccf86575`.
- Token = one OSHB `<w>` element. Type = one distinct letter-code string
  (final forms kept).

## Methods and kolel

- Method universe: the 16 methods of `src/core/methods.c`:
  - hechrachi, gadol, katan, siduri, kidmi, prati, meshulash, boneeh,
  - akhor, klali, katan-mispari, musafi,
  - atbash, albam, achbi, avgad.
- Kolel policies: strict K = {0}; loose K = {-1, 0, +1}. Both are reported
  every time, and neither is chosen after seeing a result.

## Claim auditor (the actionable output)

- **Input.** A claim is a pair of Hebrew words (A, B), or a word and a
  number (A, n). The claim declares its method set M' (default: hechrachi
  alone) and its kolel policy K.
- **Statistic.** The match share s(A) is the fraction of corpus word types
  T (T != A) for which some m in M' and some k in K give v_m(T) = v_m(A) + k.
  It is computed over all 39 books, loose word forms, and reported both
  type-weighted and token-weighted.
- **p.** The probability that a random corpus word type matches A at least
  as well as B does. The claim holds (B matches A under M', K) when
  p = s(A).
- **DoF price.** |M'| x |K|, printed beside p.
- **Verdict.**
  - p < alpha: "rare under the declared method set".
  - Otherwise: "not distinguishable from chance".
  - alpha = 0.01.
- **Out of scope.** The auditor takes Hebrew words and numbers only. It
  issues no verdict about persons, events or dates and has no
  prediction mode.

## R1: dictionary-style collision figures (WLC analogue of gamatria.app)

Computed over WLC word types, all 39 books. These are analogues, not
replications: the source used a dictionary, not this corpus.

- Pair collision rate under hechrachi.
- P(30 random types contain a shared hechrachi value): 100,000 draws.
- Median match share:
  - hechrachi alone with K = {0};
  - all 16 methods with K = {-1, 0, +1}.

Source figures for comparison: 1 in 692 (Tanakh dictionary entries);
45%; 0.13% and 34.9%.

## R2: Pentateuch value coverage (replication of Pichkhadze, "Coincidence is cheap")

Source: https://sapir861.com/research/coincidence (fetched 2026-10-30),
DOI 10.5281/zenodo.22848828.

### Statistic

Coverage is the number of values in 1..1000 that at least one Pentateuch
word has, under hechrachi.

### Word rules

- **Loose.** Every word form as written: all `<w>` ketiv letters, any part
  of speech.
- **Strict.** Prefixes stripped; nouns, verbs and adjectives only.
  - OSHB morph segments are split at `/`.
  - Leading segments whose part-of-speech letter is C, R or T are
    prefixes, provided some later segment is not a suffix (S).
  - The head is the first segment that is not a prefix. A word counts when
    the head's part of speech is N, V or A.
  - The strict letters are the word minus its prefix parts.

### Source targets

- Loose: 937. The values in 1..861 with no word are exactly
  {1, 2, 583, 599, 729, 773, 779, 801, 827, 833, 839, 847, 859}.
- Strict: 867.
- Nulls (strict): M1 947 +/- 5 (1,000 replicates); M2 938 +/- 5
  (300 replicates); the paper's earlier text gives 946.9 +/- 4.9 and
  938.1 +/- 5.1.

### Null models

- **M1.** Permute all Pentateuch letters, keeping every word's length,
  part structure and part-of-speech tags in place. 1,000 replicates.
- **M2.** A first-order Markov chain of letters, fitted on all Pentateuch
  words (initial-letter and transition counts). Each word is regenerated
  at its own length, keeping its part structure and tags. 300 replicates.
- **Report.** Mean, standard deviation, z of the observed coverage, and
  the number of replicates at or below the observed value.

### Approximations, stated in advance

- The source does not say ketiv or qere; ours is ketiv.
- Its prefix-stripping rule is not published in detail; ours is the rule
  above.
- A mismatch with the source targets is reported as measured, not tuned
  away.

## RNG

xoshiro256**, seeded through splitmix64. Replicate r of analysis a uses
seed 20261030 + 1000003 * a + r, with a = 1 for R1, 2 for M1 and 3 for M2.
