//! ASCII input for Hebrew consonants.
//!
//! Michigan-Claremont, CCAT Michigan manual section 3.3.1: the alphabet in
//! order is `)BGDHWZX+YKLMNS(PCQR&$T`; final forms are not encoded because
//! they are "completely predictable from their position in a word", so a
//! final-capable letter at the end of a word becomes its final form here.
//! Also accepted: McKay's modified scheme (A = alef, @ = ayin;
//! users.cecs.anu.edu.au/~bdm/codes/MC.html), `#` for unpointed shin/sin,
//! and lowercase (TC scheme). Hebrew letters pass through unchanged.

fn consonant(c: char) -> Option<char> {
    Some(match c.to_ascii_uppercase() {
        ')' | 'A' => 'א',
        'B' => 'ב',
        'G' => 'ג',
        'D' => 'ד',
        'H' => 'ה',
        'W' => 'ו',
        'Z' => 'ז',
        'X' => 'ח',
        '+' => 'ט',
        'Y' => 'י',
        'K' => 'כ',
        'L' => 'ל',
        'M' => 'מ',
        'N' => 'נ',
        'S' => 'ס',
        '(' | '@' => 'ע',
        'P' => 'פ',
        'C' => 'צ',
        'Q' => 'ק',
        'R' => 'ר',
        '$' | '&' | '#' => 'ש',
        'T' => 'ת',
        _ => return None,
    })
}

fn final_form(c: char) -> Option<char> {
    Some(match c {
        'כ' => 'ך',
        'מ' => 'ם',
        'נ' => 'ן',
        'פ' => 'ף',
        'צ' => 'ץ',
        _ => return None,
    })
}

/// Convert transliterated input to Hebrew; finals only for letters that
/// came from ASCII (typed Hebrew is taken exactly as written).
pub fn to_hebrew(input: &str) -> String {
    let mut out: Vec<(char, bool)> = Vec::with_capacity(input.len());
    let finish_word = |out: &mut Vec<(char, bool)>| {
        if let Some((c, true)) = out.last().copied()
            && let Some(f) = final_form(c)
        {
            out.last_mut().unwrap().0 = f;
        }
    };
    for c in input.chars() {
        if let Some(h) = consonant(c) {
            out.push((h, true));
        } else {
            if c.is_whitespace() || c == '-' || c == '\u{05BE}' {
                finish_word(&mut out);
            }
            out.push((c, false));
        }
    }
    finish_word(&mut out);
    out.into_iter().map(|(c, _)| c).collect()
}

#[cfg(test)]
mod tests {
    use super::to_hebrew;

    #[test]
    fn michigan_alphabet_order() {
        // CCAT manual 3.3.1 order maps onto the 22 letters in order (sin & before shin $).
        assert_eq!(to_hebrew(")BGDHWZX+YKLMNS(PCQR&$T"), "אבגדהוזחטיכלמנסעפצקרששת");
    }

    #[test]
    fn finals_at_word_end() {
        assert_eq!(to_hebrew("YYN SWD"), "יין סוד");
        assert_eq!(to_hebrew("MLK"), "מלך");
        assert_eq!(to_hebrew("ywm"), "יום");
    }

    #[test]
    fn hebrew_passes_through() {
        assert_eq!(to_hebrew("שלום"), "שלום");
    }
}
