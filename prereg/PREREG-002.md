# PREREG-002: R2 strict rule with suffixes stripped

Frozen 2026-10-30, after PREREG-001's R2 strict result (907, not the
source's 867) and before any null model was run under the rule below. Its
sha256 is recorded in `prereg/PREREG-002.sha256`. Everything in
PREREG-001 not named here is unchanged.

## What happened before this file

### The preregistered result, which stands

PREREG-001 R2 strict coverage was 907 against the source's 867: not
replicated.

### The exploratory diagnosis

The diagnosis was `gem-stat CORPUS r2x`, labelled exploratory and not
preregistered.

| Variant | Coverage | Missing-value list vs source |
|---|---|---|
| V0 (PREREG-001 rule) | 907 | differs |
| V1 (proper nouns excluded) | 904 | differs |
| V2 (trailing suffix parts also stripped) | 867 | identical to the source's 56 values in 1..861 |
| V3 (V1 + V2) | 861 | differs |

### What follows from it

V2's observed coverage was found by looking at the data. It is therefore
NOT evidence of anything and is not claimed as a confirmatory result.

## The rule frozen here

- **Strict rule (V2).** Strip the prefix parts (PREREG-001 definition) and
  every trailing part whose part-of-speech letter is S (pronominal
  suffix).
- **Words counted.** Nouns (including proper nouns), verbs and adjectives,
  by the head-segment rule of PREREG-001.

## Confirmatory test (not yet run when this file was frozen)

- **Nulls under V2.** M1 (1,000 replicates) and M2 (300 replicates), as
  defined in PREREG-001, with the same seeds (analyses 2 and 3).
- **Prediction (source: Pichkhadze, sapir861.com/research/coincidence).**
  - M1 mean within one source sd of 946.9 (i.e. 942.0 to 951.8).
  - M2 mean within one source sd of 938.1 (i.e. 933.0 to 943.2).
  - 0 replicates at or below the observed 867, for each null.
- **Reporting.** Each part is reported as met or not met. Nothing is
  re-run with other settings to improve the fit.
