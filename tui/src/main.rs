//! gem-tui - terminal front end of the gematria claim auditor (PREREG-001).
//!
//! Type a Hebrew word (Hebrew letters or Michigan-Claremont ASCII), or a
//! claim `A = B`. Every keystroke recomputes the values under the 16
//! methods, the corpus word types sharing a value with A, and the claim
//! verdict with its p and its price in degrees of freedom. Tab switches the
//! method set (hechrachi / all 16), F2 the kolel policy (K = 0 / +-1),
//! Ctrl-U clears, Esc quits. The screen redraws only on input or when the
//! terminal changes size. The terminal layer is src/term.rs (std and libc
//! only).
//!
//! `gem-tui [CORPUS] --once QUERY [--all] [--kolel]` prints the same panels
//! as text (same compute path) and exits.
mod ffi;
mod term;
mod translit;

use ffi::{Claim, Engine, Hit, METHODS};
use std::process::ExitCode;
use term::{Key, Screen, Style};

const HITS: usize = 40;
const HECHRACHI: u32 = 1;
const ALL: u32 = (1 << METHODS) - 1;

#[derive(Default)]
struct Outcome {
    a: String,
    b: Option<String>,
    va: Option<[u64; METHODS]>,
    vb: Option<[u64; METHODS]>,
    share: Option<Claim>,
    hits: Vec<Hit>,
    claim: Option<Claim>,
    base_rate: Option<f64>,
    error: Option<String>,
}

struct App {
    engine: Engine,
    info: ffi::Info,
    input: String,
    mask: u32,
    kolel: i32,
    out: Outcome,
}

impl App {
    fn compute(&mut self) {
        self.out = evaluate(&mut self.engine, &self.input, self.mask, self.kolel);
    }

    fn set_label(&self) -> String {
        let methods = if self.mask == HECHRACHI { "hechrachi" } else { "all 16 methods" };
        format!("{methods}, K = {}", if self.kolel == 0 { "0" } else { "+/-1" })
    }
}

fn evaluate(engine: &mut Engine, input: &str, mask: u32, kolel: i32) -> Outcome {
    let (left, right) = match input.split_once('=') {
        Some((l, r)) => (l, Some(r)),
        None => (input, None),
    };
    let mut out = Outcome { a: translit::to_hebrew(left.trim()), ..Outcome::default() };
    out.b = right.map(|r| translit::to_hebrew(r.trim())).filter(|s| !s.is_empty());
    if out.a.is_empty() {
        return out;
    }
    out.va = ffi::values(&out.a);
    if out.va.is_none() {
        out.error = Some(format!("not Hebrew: {}", left.trim()));
        return out;
    }
    if let Some((share, hits)) = engine.matches(&out.a, mask, kolel, HITS) {
        out.share = Some(share);
        out.hits = hits;
        out.base_rate = engine.base_rate(mask, kolel);
    }
    if let Some(b) = out.b.clone() {
        out.vb = ffi::values(&b);
        match out.vb {
            None => out.error = Some(format!("not Hebrew: {}", right.unwrap_or_default().trim())),
            Some(_) => out.claim = engine.audit(&out.a, &b, mask, kolel),
        }
    }
    out
}

#[derive(Clone, Copy)]
enum Tone {
    Plain,
    Good,
    Bad,
    Note,
}

fn verdict_lines(app: &App) -> Vec<(String, Tone)> {
    let o = &app.out;
    let mut v = Vec::new();
    if let Some(e) = &o.error {
        v.push((e.clone(), Tone::Bad));
        return v;
    }
    let Some(share) = o.share else {
        v.push(("type a word, or a claim A = B".into(), Tone::Note));
        return v;
    };
    let p_line = format!(
        "other corpus types matching A: {} of {}  ->  p = {:.4} (types), {:.4} (tokens)",
        share.share_types, share.other_types, share.p_types, share.p_tokens
    );
    let dof = format!("declared set: {}  ->  DoF price {}", app.set_label(), share.dof);
    match (o.claim, &o.b) {
        (Some(c), Some(_)) => {
            let via: Vec<String> = (0..METHODS).filter(|m| c.matching >> m & 1 == 1).map(ffi::method_name).collect();
            if c.holds == 0 {
                v.push((format!("A = B does NOT hold under {}", app.set_label()), Tone::Bad));
                v.push((p_line, Tone::Plain));
                v.push(("verdict: no equality to assess".into(), Tone::Note));
            } else {
                v.push((format!("A = B holds via {}", via.join(", ")), Tone::Plain));
                v.push((p_line, Tone::Plain));
                v.push((dof, Tone::Plain));
                if c.rare != 0 {
                    v.push((format!("verdict: RARE under the declared method set (p < {})", c.alpha), Tone::Good));
                } else {
                    v.push((format!("verdict: NOT DISTINGUISHABLE FROM CHANCE (p >= {})", c.alpha), Tone::Bad));
                }
            }
        }
        _ => {
            v.push((p_line, Tone::Plain));
            v.push((dof, Tone::Plain));
            let would = if share.rare != 0 { "rare" } else { "not distinguishable from chance" };
            v.push((format!("a claimed equality with A under this set would be: {would}"), Tone::Note));
        }
    }
    if let Some(r) = o.base_rate {
        v.push((
            format!("base rate: {:.1} % of ALL corpus types score p < {} under this set", 100.0 * r, share.alpha),
            Tone::Note,
        ));
        v.push((
            "p depends on A alone: the verdict prices the method set, not the pair (pair test: PREREG-003, not built)"
                .into(),
            Tone::Note,
        ));
    }
    v
}

fn tone_style(t: Tone) -> Style {
    match t {
        Tone::Plain => Style::PLAIN,
        Tone::Good => Style::fg(term::GREEN).bold(),
        Tone::Bad => Style::fg(term::RED).bold(),
        Tone::Note => Style::fg(term::YELLOW),
    }
}

// Layout: head line, query box (3 rows), body (values left, matches right),
// verdict box, key line. Column offsets inside the two tables.
const QUERY_ROWS: u16 = 3;
const VERDICT_ROWS: u16 = 8;
const BODY_MIN_ROWS: u16 = 12;
const VALUES_WIDTH: u16 = 48;
const VALUE_COLS: [(u16, u16); 4] = [(1, 14), (16, 13), (30, 2), (33, 13)];
const HIT_COLS: [(u16, u16); 2] = [(1, 7), (9, 13)];
const HIT_WORD_COL: u16 = 23;

fn draw(scr: &mut Screen, app: &App) -> std::io::Result<()> {
    let (w, h) = scr.size();
    let o = &app.out;
    let body_y = 1 + QUERY_ROWS;
    let body_rows = h.saturating_sub(body_y + VERDICT_ROWS + 1).max(BODY_MIN_ROWS);
    let verdict_y = body_y + body_rows;
    scr.begin();

    let x = scr.text(0, 0, w, " gematria-re ", Style::BOLD);
    let head = format!("| WLC 4.20 | {} types, {} tokens | frozen corpus + preregs: ", app.info.types, app.info.tokens);
    let x = scr.text(x, 0, w, &head, Style::PLAIN);
    if app.info.frozen_ok {
        scr.text(x, 0, w, "VERIFIED", Style::fg(term::GREEN).bold());
    } else {
        scr.text(x, 0, w, "NOT VERIFIED", Style::fg(term::RED).bold());
    }

    scr.frame(0, 1, w, QUERY_ROWS, " query: a word, or a claim A = B (Hebrew, or Michigan-Claremont ASCII) ");
    let shown = match (&o.a, &o.b) {
        (a, Some(b)) => format!("   A = {a}   B = {b}"),
        (a, None) if !a.is_empty() => format!("   A = {a}"),
        _ => String::new(),
    };
    let inner = w.saturating_sub(2);
    let x = scr.text(1, 2, inner, "> ", Style::BOLD);
    let x = scr.text(x, 2, inner.saturating_sub(x - 1), &app.input, Style::PLAIN);
    let x = scr.text(x, 2, inner.saturating_sub(x - 1), "_", Style::DIM);
    scr.text(x, 2, inner.saturating_sub(x - 1), &shown, Style::fg(term::CYAN));

    scr.frame(0, body_y, VALUES_WIDTH.min(w), body_rows, " values (declared set in bold) ");
    for (&(col, width), title) in VALUE_COLS.iter().zip(["method", "A", "", "B"]) {
        scr.text(col, body_y + 1, width, &format!("{title:<w$}", w = width as usize), Style::UNDERLINE);
    }
    let matching = o.claim.map(|c| c.matching).unwrap_or(0);
    for m in 0..METHODS.min(body_rows.saturating_sub(3) as usize) {
        let y = body_y + 2 + m as u16;
        let style = if app.mask >> m & 1 == 1 { Style::BOLD } else { Style::DIM };
        let cells = [
            ffi::method_name(m),
            o.va.map(|v| v[m].to_string()).unwrap_or_default(),
            if matching >> m & 1 == 1 { "=".to_string() } else { String::new() },
            o.vb.map(|v| v[m].to_string()).unwrap_or_default(),
        ];
        for (&(col, width), cell) in VALUE_COLS.iter().zip(&cells) {
            scr.text(col, y, width, cell, style);
        }
    }

    // The Hebrew word is the LAST column: in a terminal that applies the
    // Unicode BiDi algorithm (Konsole), digits after a Hebrew run become
    // Arabic numbers (rule W2) and are reversed with it, swapping cells.
    let hits_x = VALUES_WIDTH;
    let hits_w = w.saturating_sub(hits_x);
    let title = match o.share {
        Some(s) => format!(" corpus types sharing a value with A: {} (most frequent first) ", s.share_types),
        None => " corpus types sharing a value with A ".to_string(),
    };
    scr.frame(hits_x, body_y, hits_w, body_rows, &title);
    let word_w = hits_w.saturating_sub(HIT_WORD_COL + 1);
    for (&(col, width), title) in HIT_COLS.iter().zip(["tokens", "first"]) {
        scr.text(hits_x + col, body_y + 1, width, &format!("{title:<w$}", w = width as usize), Style::UNDERLINE);
    }
    scr.text(hits_x + HIT_WORD_COL, body_y + 1, word_w, &format!("{:<w$}", "word", w = word_w as usize), Style::UNDERLINE);
    for (row, hit) in o.hits.iter().take(body_rows.saturating_sub(3) as usize).enumerate() {
        let y = body_y + 2 + row as u16;
        scr.text(hits_x + HIT_COLS[0].0, y, HIT_COLS[0].1, &hit.tokens.to_string(), Style::PLAIN);
        scr.text(hits_x + HIT_COLS[1].0, y, HIT_COLS[1].1, &hit.first, Style::PLAIN);
        scr.text(hits_x + HIT_WORD_COL, y, word_w, &hit.word, Style::PLAIN);
    }

    scr.frame(0, verdict_y, w, VERDICT_ROWS, " claim auditor (PREREG-001, alpha 0.01; words only: no persons, events or dates) ");
    for (row, (line, tone)) in verdict_lines(app).into_iter().take((VERDICT_ROWS - 2) as usize).enumerate() {
        scr.text(1, verdict_y + 1 + row as u16, inner, &line, tone_style(tone));
    }
    let keys = format!(" Tab: methods [{}]   F2: kolel   Ctrl-U: clear   Esc: quit", app.set_label());
    scr.text(0, verdict_y + VERDICT_ROWS, w, &keys, Style::DIM);
    scr.end()
}

fn run(scr: &mut Screen, app: &mut App) -> std::io::Result<()> {
    draw(scr, app)?;
    loop {
        let keys = term::read_keys(250);
        for key in &keys {
            match *key {
                Key::Esc | Key::CtrlC => return Ok(()),
                Key::CtrlU => app.input.clear(),
                Key::Tab => app.mask = if app.mask == HECHRACHI { ALL } else { HECHRACHI },
                Key::F2 => app.kolel = 1 - app.kolel,
                Key::Backspace => {
                    app.input.pop();
                }
                Key::Char(c) => app.input.push(c),
            }
        }
        if !keys.is_empty() {
            app.compute();
        }
        if scr.resized() || !keys.is_empty() {
            draw(scr, app)?;
        }
    }
}

fn print_once(app: &App) {
    println!("gematria-re | WLC 4.20 | {} types | {}", app.info.types, app.info.frozen);
    println!("query: {}  ->  A = {}{}", app.input, app.out.a, app.out.b.as_ref().map(|b| format!(", B = {b}")).unwrap_or_default());
    if let Some(va) = app.out.va {
        for m in 0..METHODS {
            let b = app.out.vb.map(|v| format!("  B {}", v[m])).unwrap_or_default();
            let inset = if app.mask >> m & 1 == 1 { "*" } else { " " };
            println!("  {inset} {:<14} A {}{b}", ffi::method_name(m), va[m]);
        }
    }
    for h in app.out.hits.iter().take(8) {
        println!("  match x{:<5} first {:<11} {}", h.tokens, h.first, h.word);
    }
    for (line, _) in verdict_lines(app) {
        println!("  {line}");
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut corpus = env!("GEM_DEFAULT_CORPUS").to_string();
    let (mut once, mut mask, mut kolel) = (None, HECHRACHI, 0);
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--once" => once = it.next().cloned(),
            "--all" => mask = ALL,
            "--kolel" => kolel = 1,
            path => corpus = path.to_string(),
        }
    }
    let engine = match Engine::open(&corpus) {
        Ok(e) => e,
        Err(e) => {
            eprintln!("gem-tui: {e}");
            return ExitCode::FAILURE;
        }
    };
    let info = engine.info();
    let mut app = App { engine, info, input: once.clone().unwrap_or_default(), mask, kolel, out: Outcome::default() };
    app.compute();
    if once.is_some() {
        print_once(&app);
        return ExitCode::SUCCESS;
    }
    term::install_panic_restore();
    let result = Screen::open().and_then(|mut screen| run(&mut screen, &mut app));
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("gem-tui: {e}");
            ExitCode::FAILURE
        }
    }
}
