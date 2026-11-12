# PREREG-005: is the traditional letter order unusual in the four-system agreement it produces?

Frozen 2026-11-12, before any code for this test existed and before any
value under a shuffled letter order was computed. Its sha256 is recorded in
`prereg/PREREG-005.sha256`. The witness, corpus and RNG are those of
PREREG-001.

## Background, known before the freeze

Stage K (exploratory, `results/exploratory-kab-graph.txt`) counted, over the
39,912 word types of WLC plus Sefer Yetzirah, 103,310 pairs of types with
different letter multisets that agree in hechrachi, gadol, katan and siduri
at once, against 0.706 expected for four independent systems. The cause is
algebraic and holds for every letter: hechrachi = katan x 10^d and
siduri = katan + 9d (d = 0, 1, 2 for a units, tens or hundreds letter).
That identity holds under any assignment of the 22 letters to the 22
ordinals. What it does not answer: among all such assignments, is the
traditional one unusual in how many agreeing pairs it yields on the real
lexicon? The count for WLC alone has not been computed.

## Units

- **Lexicon.** The 39,615 word types of all 39 books under the loose rule
  of PREREG-001 (an OSHB `<w>` word, all its letters, finals kept as
  written).
- **Letter order.** A bijection from the 22 base letters to the ordinals
  1..22. The traditional order sends alef..tav to 1..22.
- **Values under an order.** For a letter with ordinal o:
  - siduri = o;
  - hechrachi = o for o <= 9, 10 x (o - 9) for o <= 18, 100 x (o - 18)
    otherwise;
  - katan = hechrachi with its zeros dropped;
  - gadol = hechrachi, except for final forms. The five letters that have
    final forms are a property of the script and stay kaf, mem, nun, pe,
    tsadi under every order; taken in ascending order of their ordinals,
    their final forms get 500, 600, 700, 800, 900.
  Under the traditional order these are exactly the tables of
  `include/gem/kab/tables.hpp`.
- **Qualifying pair.** An unordered pair of distinct types with different
  letter multisets (finals folded).

## Statistics

- **T (primary).** The number of qualifying pairs that agree in all four
  systems.
- **T_H (secondary).** The number of qualifying pairs that agree in
  hechrachi.
- **C (secondary).** T / T_H.

## Null

- 1,000 random letter orders. Order r is a Fisher-Yates shuffle of
  p = (0, 1, ..., 21): for i = 21 down to 1, j = below(i + 1), swap p[i]
  and p[j]; the letter at alphabet position i gets ordinal p[i] + 1. The
  generator is the Rng of PREREG-001 seeded with
  replicate_seed(20261030, 6, r), r = 0..999.
- For each statistic X: p_hi = (1 + #{X_r >= X_obs}) / 1001,
  p_lo = (1 + #{X_r <= X_obs}) / 1001, p = min(1, 2 x min(p_hi, p_lo)).

## Decision

- **Primary.** T at alpha = 0.01, two-sided.
- **Secondary.** T_H and C, each at alpha = 0.005 (Bonferroni over two).
- **Gate.** T and T_H under the traditional order must equal the stage K
  census of the same lexicon (gem-kab on the WLC text alone: the pairs
  with different multisets agreeing in at least HGKS, and in at least
  H---). The gate is checked after this freeze; a mismatch voids the run.
- **Reading, fixed in advance.** A significant result says that the
  traditional order is unusual among the 22! orders in the agreement it
  produces on this lexicon. It says nothing about the construction of the
  text: the dependence between the systems holds under every order, and
  where the frequent letters sit in the order is enough to move T. If the
  result is significant, the exploratory follow-up is the rank correlation
  between a letter's token frequency and its ordinal.
- **Expectation stated before the run (mine).** T_obs lies above the
  median of the shuffled orders, because several of the most frequent
  letters carry small values in the traditional order; it does not reach
  significance, p >= 0.01.
- **Reporting.** All three statistics are reported with their observed
  value, the mean, sd, minimum and maximum over the 1,000 orders, p_hi,
  p_lo and p, significant or not. A bug fix after the run is reported as
  such, with both numbers.
