# PREREG-003: do equal-valued words cluster in the text?

Frozen 2026-10-30, before any code for this test existed. Its sha256 is
recorded in `prereg/PREREG-003.sha256`. The witness, corpus and RNG are
those of PREREG-001.

## Question

The design reading of gematria holds that words of equal value are linked
by the text's construction. If so, equal-valued pairs of genuinely
different words should co-occur within a verse or chapter more often than
chance. And that excess should belong to the standard letter values
specifically, not to any assignment of values to letters.

## Units

- **Token.** An OSHB `<w>` word, all its letters (loose rule). Finals fold
  to their base letter for values; the standard (hechrachi) value table.
- **Qualifying pair.** An unordered pair of tokens i != j that meets all
  of the following:
  - **Same value.** v(i) = v(j) for K = 0 (primary). For K = +/-1
    (secondary), |v(i) - v(j)| <= 1.
  - **Different lemma.** The lemma key is the OSHB `lemma` attribute split
    at `/`, keeping only the segments that contain a digit, rejoined with
    `/`. Prefix codes such as b, c, d, l, k, m drop out.
  - **Different letter multiset.** The multiset is computed with finals
    folded, which excludes repeated words and anagrams.
- **Window.** A verse (primary) or a chapter (secondary).
- **Scope.**
  - Pentateuch: Genesis to Deuteronomy (primary).
  - All 39 books (secondary).

## Statistics

- **S.** The number of qualifying pairs that fall in the same window,
  summed over all windows of the scope.
- **E (exact).** The null expectation under the M3 shuffle below. For
  each book, E_book = Q_book x sum over its windows of C(n_w, 2), where
  Q_book = (qualifying pairs among all tokens of the book) / C(N_book, 2).
  E = sum of E_book over the books of the scope.
- **R.** The excess ratio S / E.

## Test Q1: is there any excess?

- **Null M3.** Within each book, permute the tokens across positions. Each
  window keeps its token count; the book keeps its token multiset.
- 1,000 replicates under the standard table.
- **Report.** S, E, the replicate mean and sd, z, and the one-sided
  p = (1 + #{S_rep >= S}) / 1001.

## Test Q2: is the excess specific to the standard values? (PRIMARY)

- **Placebo tables.** 1,000 random value tables: uniform random
  permutations of the 22 standard values over the 22 base letters.
  Finals fold, so all tables are on equal footing.
- **Statistic.** R for each placebo table, computed exactly by the formula
  above.
- **p.** (1 + #{R_placebo >= R_standard}) / 1001.
- **Seeds.** Placebo table t uses replicate_seed(20261030, 4, t). M3
  replicate r uses replicate_seed(20261030, 5, r).

## Decision

- **Primary.** Q2 in the Pentateuch, verse window, K = 0, at alpha = 0.01.
- **Secondary.** The other 7 combinations of scope x window x K, for Q1
  and for Q2. They are reported with Bonferroni alpha = 0.01 / 8 =
  0.00125 within each question.
- **Design prediction.** Q2 primary p < 0.01.
- **Expectation stated before the run (mine).** No significant Q2 excess:
  equal-valued pairs are not specially placed together.
- **Reporting.** Every combination is reported, significant or not. A
  bug fix after the run is reported as such, with both numbers.
