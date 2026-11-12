//! term.rs - the terminal layer of gem-tui: std and libc only, no terminal
//! crates. Raw mode through termios, size through TIOCGWINSZ, input through
//! poll + read with an own key decoder, output as one buffered write per
//! frame in which every piece of text carries its absolute cursor position.
use std::io::Write;
use std::os::raw::c_int;
use std::sync::Mutex;

// ---- libc (x86-64 Linux, glibc layouts) -----------------------------------
#[repr(C)]
#[derive(Clone, Copy)]
struct Termios {
    c_iflag: u32,
    c_oflag: u32,
    c_cflag: u32,
    c_lflag: u32,
    c_line: u8,
    c_cc: [u8; 32],
    c_ispeed: u32,
    c_ospeed: u32,
}

#[repr(C)]
struct Winsize {
    ws_row: u16,
    ws_col: u16,
    ws_xpixel: u16,
    ws_ypixel: u16,
}

#[repr(C)]
struct PollFd {
    fd: c_int,
    events: i16,
    revents: i16,
}

unsafe extern "C" {
    fn tcgetattr(fd: c_int, t: *mut Termios) -> c_int;
    fn tcsetattr(fd: c_int, act: c_int, t: *const Termios) -> c_int;
    fn cfmakeraw(t: *mut Termios);
    fn ioctl(fd: c_int, req: u64, ...) -> c_int;
    fn poll(fds: *mut PollFd, n: u64, timeout: c_int) -> c_int;
    fn read(fd: c_int, buf: *mut std::ffi::c_void, n: usize) -> isize;
}

const TCSANOW: c_int = 0;
const TIOCGWINSZ: u64 = 0x5413;
const POLLIN: i16 = 1;
const ENTER_SCREEN: &[u8] = b"\x1b[?1049h\x1b[?25l\x1b[2J";
const LEAVE_SCREEN: &[u8] = b"\x1b[0m\x1b[?25h\x1b[?1049l";

/// The terminal settings to restore; set while a Screen is open, so the
/// panic hook can restore them too.
static SAVED: Mutex<Option<Termios>> = Mutex::new(None);

fn restore_terminal() {
    if let Ok(mut saved) = SAVED.lock()
        && let Some(t) = saved.take()
    {
        let mut out = std::io::stdout().lock();
        let _ = out.write_all(LEAVE_SCREEN);
        let _ = out.flush();
        unsafe { tcsetattr(0, TCSANOW, &t) };
    }
}

/// Restore the terminal if the program panics while in raw mode.
pub fn install_panic_restore() {
    let previous = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        restore_terminal();
        previous(info);
    }));
}

// ---- output -----------------------------------------------------------------
pub const RED: u8 = 31;
pub const GREEN: u8 = 32;
pub const YELLOW: u8 = 33;
pub const CYAN: u8 = 36;

/// Text attributes; `fg` is an SGR colour code (31..36) or 0 for the default.
#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub struct Style {
    pub fg: u8,
    pub bold: bool,
    pub dim: bool,
    pub underline: bool,
}

impl Style {
    pub const PLAIN: Style = Style { fg: 0, bold: false, dim: false, underline: false };
    pub const BOLD: Style = Style { bold: true, ..Style::PLAIN };
    pub const DIM: Style = Style { dim: true, ..Style::PLAIN };
    pub const UNDERLINE: Style = Style { underline: true, ..Style::PLAIN };

    pub const fn fg(code: u8) -> Style {
        Style { fg: code, ..Style::PLAIN }
    }

    pub const fn bold(self) -> Style {
        Style { bold: true, ..self }
    }

    /// The SGR sequence that selects this style; empty for the plain style.
    /// Colours are left out when `color` is false (NO_COLOR).
    fn sgr(self, color: bool) -> String {
        let mut codes: Vec<String> = Vec::new();
        if self.bold {
            codes.push("1".into());
        }
        if self.dim {
            codes.push("2".into());
        }
        if self.underline {
            codes.push("4".into());
        }
        if self.fg != 0 && color {
            codes.push(self.fg.to_string());
        }
        if codes.is_empty() { String::new() } else { format!("\x1b[{}m", codes.join(";")) }
    }
}

/// The terminal in raw mode on the alternate screen. Dropping it restores
/// the terminal.
pub struct Screen {
    out: Vec<u8>,
    w: u16,
    h: u16,
    color: bool,
}

impl Screen {
    pub fn open() -> std::io::Result<Screen> {
        let mut t: Termios = unsafe { std::mem::zeroed() };
        if unsafe { tcgetattr(0, &mut t) } != 0 {
            return Err(std::io::Error::last_os_error());
        }
        let saved = t;
        unsafe { cfmakeraw(&mut t) };
        if unsafe { tcsetattr(0, TCSANOW, &t) } != 0 {
            return Err(std::io::Error::last_os_error());
        }
        if let Ok(mut slot) = SAVED.lock() {
            *slot = Some(saved);
        }
        let color = !std::env::var_os("NO_COLOR").is_some_and(|v| !v.is_empty());
        let mut screen = Screen { out: Vec::with_capacity(1 << 14), w: 0, h: 0, color };
        screen.resized();
        let mut out = std::io::stdout().lock();
        out.write_all(ENTER_SCREEN)?;
        out.flush()?;
        Ok(screen)
    }

    pub fn size(&self) -> (u16, u16) {
        (self.w, self.h)
    }

    /// Re-reads the terminal size (80 x 24 if the terminal reports none);
    /// true when it changed.
    pub fn resized(&mut self) -> bool {
        let mut ws = Winsize { ws_row: 0, ws_col: 0, ws_xpixel: 0, ws_ypixel: 0 };
        let known = unsafe { ioctl(1, TIOCGWINSZ, &mut ws as *mut Winsize) } == 0 && ws.ws_col != 0 && ws.ws_row != 0;
        let (w, h) = if known { (ws.ws_col, ws.ws_row) } else { (80, 24) };
        let changed = (w, h) != (self.w, self.h);
        (self.w, self.h) = (w, h);
        changed
    }

    /// Start a frame: everything drawn until `end` goes out as one write.
    pub fn begin(&mut self) {
        self.out.clear();
        self.out.extend_from_slice(b"\x1b[0m\x1b[H\x1b[2J");
    }

    /// Up to `max` characters of `s` at column x, row y (0-based), clipped
    /// to the screen. Returns the column after the text.
    pub fn text(&mut self, x: u16, y: u16, max: u16, s: &str, style: Style) -> u16 {
        if y >= self.h || x >= self.w {
            return x;
        }
        let room = max.min(self.w - x) as usize;
        let shown: String = s.chars().take(room).collect();
        let sgr = style.sgr(self.color);
        let _ = write!(self.out, "\x1b[{};{}H{sgr}{shown}", y + 1, x + 1);
        if !sgr.is_empty() {
            self.out.extend_from_slice(b"\x1b[0m");
        }
        x + shown.chars().count() as u16
    }

    /// A single-line box with its title in the top border.
    pub fn frame(&mut self, x: u16, y: u16, w: u16, h: u16, title: &str) {
        if w < 2 || h < 2 {
            return;
        }
        let inner = (w - 2) as usize;
        let title: String = title.chars().take(inner).collect();
        let top = format!("┌{title}{}┐", "─".repeat(inner - title.chars().count()));
        self.text(x, y, w, &top, Style::PLAIN);
        for row in 1..h - 1 {
            self.text(x, y + row, 1, "│", Style::PLAIN);
            self.text(x + w - 1, y + row, 1, "│", Style::PLAIN);
        }
        self.text(x, y + h - 1, w, &format!("└{}┘", "─".repeat(inner)), Style::PLAIN);
    }

    /// Send the frame.
    pub fn end(&mut self) -> std::io::Result<()> {
        let mut out = std::io::stdout().lock();
        out.write_all(&self.out)?;
        out.flush()
    }
}

impl Drop for Screen {
    fn drop(&mut self) {
        restore_terminal();
    }
}

// ---- input ------------------------------------------------------------------
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Key {
    Char(char),
    Esc,
    Backspace,
    Tab,
    F2,
    CtrlC,
    CtrlU,
}

/// Keys available within `timeout_ms` (empty on timeout). A read that ends
/// inside an escape sequence waits up to 30 ms for the rest before decoding,
/// which is also how a lone Esc is told from the start of a sequence.
pub fn read_keys(timeout_ms: i32) -> Vec<Key> {
    let mut p = PollFd { fd: 0, events: POLLIN, revents: 0 };
    if unsafe { poll(&mut p, 1, timeout_ms) } <= 0 {
        return vec![];
    }
    let mut buf = [0u8; 1024];
    let mut n = 0usize;
    loop {
        let r = unsafe { read(0, buf[n..].as_mut_ptr() as *mut std::ffi::c_void, buf.len() - n) };
        if r <= 0 {
            break;
        }
        n += r as usize;
        if n == buf.len() || !incomplete(&buf[..n]) {
            break;
        }
        let mut more = PollFd { fd: 0, events: POLLIN, revents: 0 };
        if unsafe { poll(&mut more, 1, 30) } <= 0 {
            break;
        }
    }
    decode(&buf[..n])
}

/// True when the bytes end inside an escape sequence: ESC, ESC [ or ESC O,
/// then only parameter bytes (digits, ';').
fn incomplete(b: &[u8]) -> bool {
    let Some(e) = b.iter().rposition(|&c| c == 0x1b) else { return false };
    let tail = &b[e + 1..];
    match tail.first() {
        None => true,
        Some(b'[') | Some(b'O') => tail[1..].iter().all(|c| c.is_ascii_digit() || *c == b';'),
        Some(_) => false,
    }
}

/// Decode the bytes of one read. F2 arrives as ESC O Q or ESC [ 12 ~; every
/// other escape sequence is skipped; an ESC that starts no sequence is Esc.
pub fn decode(b: &[u8]) -> Vec<Key> {
    let mut keys = vec![];
    let mut i = 0;
    while i < b.len() {
        let c = b[i];
        match c {
            0x1b => {
                if i + 1 < b.len() && (b[i + 1] == b'[' || b[i + 1] == b'O') {
                    let mut j = i + 2;
                    while j < b.len() && (b[j].is_ascii_digit() || b[j] == b';') {
                        j += 1;
                    }
                    if j < b.len() {
                        let params = &b[i + 2..j];
                        if (b[i + 1] == b'O' && b[j] == b'Q') || (b[j] == b'~' && params == b"12") {
                            keys.push(Key::F2);
                        }
                        i = j + 1;
                        continue;
                    }
                }
                keys.push(Key::Esc);
                i += 1;
            }
            b'\t' => {
                keys.push(Key::Tab);
                i += 1;
            }
            0x7f | 0x08 => {
                keys.push(Key::Backspace);
                i += 1;
            }
            0x03 => {
                keys.push(Key::CtrlC);
                i += 1;
            }
            0x15 => {
                keys.push(Key::CtrlU);
                i += 1;
            }
            _ if c < 0x20 => i += 1,
            _ => {
                let len = match c {
                    0x00..=0x7f => 1,
                    0xc0..=0xdf => 2,
                    0xe0..=0xef => 3,
                    0xf0..=0xf7 => 4,
                    _ => 1,
                };
                let end = (i + len).min(b.len());
                if let Ok(s) = std::str::from_utf8(&b[i..end])
                    && let Some(ch) = s.chars().next()
                {
                    keys.push(Key::Char(ch));
                }
                i = end;
            }
        }
    }
    keys
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_every_key_the_interface_uses() {
        assert_eq!(decode(b"a\t\x7f\x03\x15"), [Key::Char('a'), Key::Tab, Key::Backspace, Key::CtrlC, Key::CtrlU]);
        assert_eq!(decode(b"\x1bOQ"), [Key::F2]);
        assert_eq!(decode(b"\x1b[12~"), [Key::F2]);
        assert_eq!(decode(b"\x1b"), [Key::Esc]);
        assert_eq!(decode("יין".as_bytes()), [Key::Char('י'), Key::Char('י'), Key::Char('ן')]);
    }

    #[test]
    fn skips_other_escape_sequences() {
        assert_eq!(decode(b"\x1b[A\x1b[1;5C\x1b[3~x"), [Key::Char('x')]);
        assert_eq!(decode(b"\x1bOP"), []);
    }

    #[test]
    fn knows_an_unfinished_sequence() {
        assert!(incomplete(b"\x1b"));
        assert!(incomplete(b"\x1b[12"));
        assert!(incomplete(b"\x1bO"));
        assert!(!incomplete(b"\x1b[12~"));
        assert!(!incomplete(b"abc"));
    }

    #[test]
    fn style_sequences() {
        assert_eq!(Style::PLAIN.sgr(true), "");
        assert_eq!(Style::fg(GREEN).bold().sgr(true), "\x1b[1;32m");
        assert_eq!(Style::fg(GREEN).bold().sgr(false), "\x1b[1m");
        assert_eq!(Style::UNDERLINE.sgr(true), "\x1b[4m");
    }
}
