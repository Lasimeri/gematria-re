//! gem-tui - terminal front end of the gematria claim auditor (PREREG-001).
//!
//! Type a Hebrew word (Hebrew letters or Michigan-Claremont ASCII), or a
//! claim `A = B`. Every keystroke recomputes the values under the 16
//! methods, the corpus word types sharing a value with A, and the claim
//! verdict with its p and its price in degrees of freedom. Tab switches the
//! method set (hechrachi / all 16), F2 the kolel policy (K = 0 / +-1),
//! Ctrl-U clears, Esc quits. The screen redraws only on input.
//!
//! `gem-tui [CORPUS] --once QUERY [--all] [--kolel]` prints the same panels
//! as text (same compute path) and exits.
mod ffi;
mod translit;

use ffi::{Claim, Engine, Hit, METHODS};
use ratatui::{
    DefaultTerminal, Frame,
    crossterm::event::{self, Event, KeyCode, KeyEventKind, KeyModifiers},
    layout::{Constraint, Layout},
    style::{Color, Style, Stylize},
    text::{Line, Span},
    widgets::{Block, Paragraph, Row, Table},
};
use std::process::ExitCode;

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
        Tone::Plain => Style::new(),
        Tone::Good => Style::new().fg(Color::Green).bold(),
        Tone::Bad => Style::new().fg(Color::Red).bold(),
        Tone::Note => Style::new().fg(Color::Yellow),
    }
}

fn value_rows(app: &App) -> Vec<Row<'static>> {
    let o = &app.out;
    let matching = o.claim.map(|c| c.matching).unwrap_or(0);
    (0..METHODS)
        .map(|m| {
            let a = o.va.map(|v| v[m].to_string()).unwrap_or_default();
            let b = o.vb.map(|v| v[m].to_string()).unwrap_or_default();
            let mark = if matching >> m & 1 == 1 { "=" } else { "" };
            let style = if app.mask >> m & 1 == 1 { Style::new().bold() } else { Style::new().dim() };
            Row::new(vec![ffi::method_name(m), a, mark.to_string(), b]).style(style)
        })
        .collect()
}

fn draw(f: &mut Frame, app: &App) {
    let [head, query, body, verdict, keys] = Layout::vertical([
        Constraint::Length(1),
        Constraint::Length(3),
        Constraint::Min(12),
        Constraint::Length(8),
        Constraint::Length(1),
    ])
    .areas(f.area());

    let frozen = if app.info.frozen_ok { "VERIFIED".green().bold() } else { "NOT VERIFIED".red().bold() };
    f.render_widget(
        Line::from(vec![
            " gematria-re ".bold(),
            Span::raw(format!("| WLC 4.20 | {} types, {} tokens | frozen corpus + preregs: ", app.info.types, app.info.tokens)),
            frozen,
        ]),
        head,
    );

    let shown = match (&app.out.a, &app.out.b) {
        (a, Some(b)) => format!("   A = {a}   B = {b}"),
        (a, None) if !a.is_empty() => format!("   A = {a}"),
        _ => String::new(),
    };
    f.render_widget(
        Paragraph::new(Line::from(vec!["> ".bold(), Span::raw(app.input.clone()), "_".dim(), shown.cyan()]))
            .block(Block::bordered().title(" query: a word, or a claim A = B (Hebrew, or Michigan-Claremont ASCII) ")),
        query,
    );

    let [left, right] = Layout::horizontal([Constraint::Length(48), Constraint::Min(30)]).areas(body);
    let values = Table::new(value_rows(app), [Constraint::Length(14), Constraint::Length(13), Constraint::Length(2), Constraint::Length(13)])
        .header(Row::new(vec!["method", "A", "", "B"]).underlined())
        .block(Block::bordered().title(" values (declared set in bold) "));
    f.render_widget(values, left);

    // The Hebrew word is the LAST column: in a terminal that applies the
    // Unicode BiDi algorithm (Konsole), digits after a Hebrew run become
    // Arabic numbers (rule W2) and are reversed with it, swapping cells.
    let rows: Vec<Row> = app
        .out
        .hits
        .iter()
        .map(|h| Row::new(vec![h.tokens.to_string(), h.first.clone(), h.word.clone()]))
        .collect();
    let title = match app.out.share {
        Some(s) => format!(" corpus types sharing a value with A: {} (most frequent first) ", s.share_types),
        None => " corpus types sharing a value with A ".to_string(),
    };
    let hits = Table::new(rows, [Constraint::Length(7), Constraint::Length(13), Constraint::Min(12)])
        .header(Row::new(vec!["tokens", "first", "word"]).underlined())
        .block(Block::bordered().title(title));
    f.render_widget(hits, right);

    let lines: Vec<Line> = verdict_lines(app).into_iter().map(|(s, t)| Line::styled(s, tone_style(t))).collect();
    f.render_widget(
        Paragraph::new(lines).block(Block::bordered().title(" claim auditor (PREREG-001, alpha 0.01; words only: no persons, events or dates) ")),
        verdict,
    );
    f.render_widget(
        Line::from(format!(" Tab: methods [{}]   F2: kolel   Ctrl-U: clear   Esc: quit", app.set_label())).dim(),
        keys,
    );
}

fn run(terminal: &mut DefaultTerminal, app: &mut App) -> std::io::Result<()> {
    loop {
        terminal.draw(|f| draw(f, app))?;
        let Event::Key(k) = event::read()? else { continue };
        if k.kind != KeyEventKind::Press {
            continue;
        }
        let ctrl = k.modifiers.contains(KeyModifiers::CONTROL);
        match k.code {
            KeyCode::Esc => return Ok(()),
            KeyCode::Char('c') if ctrl => return Ok(()),
            KeyCode::Char('u') if ctrl => app.input.clear(),
            KeyCode::Tab => app.mask = if app.mask == HECHRACHI { ALL } else { HECHRACHI },
            KeyCode::F(2) => app.kolel = 1 - app.kolel,
            KeyCode::Backspace => {
                app.input.pop();
            }
            KeyCode::Char(c) if !ctrl => app.input.push(c),
            _ => continue,
        }
        app.compute();
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
    let mut terminal = ratatui::init();
    let result = run(&mut terminal, &mut app);
    ratatui::restore();
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("gem-tui: {e}");
            ExitCode::FAILURE
        }
    }
}
