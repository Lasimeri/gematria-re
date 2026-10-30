# PREREG-004: equidistant letter sequences (ELS)

Frozen 2026-10-30, before any ELS code existed and before the WLC was
searched for any ELS. Its sha256 is recorded in `prereg/PREREG-004.sha256`.
The witness and RNG are those of PREREG-001.

## Text conventions

- The letter stream of a span is the corpus letters in order, with finals
  folded to their base letter (22-letter alphabet). Word boundaries,
  points and paragraph markers are not letters.
- An ELS of a word w = (w0 .. w(k-1)) with skip d is a start position i
  such that L[i + j*d] = w_j for j = 0 .. k-1.
- d > 0 reads forward. d < 0 reads backward and is counted as a forward
  occurrence of the reversed word.
- |d| = 1 (surface text) is never counted.

## Part A: the Rambsel/Jeffrey claim

Sources: Rambsel 1996 "Yeshua", via Grant Jeffrey, The Signature of God
(1996), "one chance in 50 quadrillion" (2e-17).

The passage is Isaiah 52:13 to 53:12. The letter frequencies are those of
the whole Tanakh letter stream.

- **A1 (descriptive).** Does ישועשמי (7 letters) occur with d = -20 at the
  start the claim names: the second yod of יאריך in Isaiah 53:10, counting
  back 20 letters at a time? The answer is yes or no.
- **A2.** For the 4-letter ישוע with 2 <= |d| <= 20 in the passage, report:
  - the observed count;
  - the expectation under independent letters with Tanakh frequencies;
  - 1,000 permutations of the passage's letters (analysis 7): mean, sd,
    and one-sided p = (1 + #{rep >= observed}) / 1001.
- **A3.** For the 7-letter ישועשמי with 2 <= |d| <= 20, report:
  - expected count and P(at least one) = 1 - exp(-E) in the passage;
  - expected and observed counts in the whole Tanakh.
  - Compare P(at least one) in the passage with the claimed 2e-17.

## Part B: do a text's own words appear as ELS more than chance?

- **Texts.** Genesis (primary) and Psalms (control).
- **Targets.** The word types of the text itself: loose rule, finals
  folded, exactly 5 letters, at least 5 tokens in that text. Every target
  is searched forward and reversed. The list sizes are reported.
- **Statistic.** T = total ELS occurrences of the targets over
  2 <= d <= 1000 and both directions.
- **Null M1.** 1,000 permutations of the text's own letters (analysis 8
  for Genesis, 9 for Psalms): mean, sd, z, and one-sided
  p = (1 + #{T_rep >= T}) / 1001.
- **Design prediction.** Genesis p < 0.01. Psalms is the control: no
  prediction.
- **Expectation stated before the run (mine).** No excess. McKay et al.
  1999 found that ELS frequencies follow letter statistics.
- **Breakdown.** Descriptive only, not tested: T by skip band (2-10,
  11-100, 101-1000).

## Kernel rule

- The production counter may use any algorithm, provided it matches a
  plain per-word reference scan exactly on a check run.
- The faster one is chosen by a measured A/B on the run host.
- The Xeon Phi 7120A is used only if it wins a measured A/B against the
  host kernel.
