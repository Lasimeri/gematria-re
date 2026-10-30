//! Safe wrapper over the backend's C ABI (include/gem/gemstat.h).
//! The structs mirror the C declarations field for field (#[repr(C)]).
use std::ffi::{CStr, CString, c_char, c_int, c_long, c_uint};

pub const METHODS: usize = 16;

#[repr(C)]
struct RawInfo {
    types: u32,
    tokens: u64,
    frozen_ok: c_int,
    frozen: [c_char; 200],
}

/// Claim auditor result (struct gemstat_claim).
#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct Claim {
    pub holds: c_int,
    pub matching: u32,
    pub share_types: u32,
    pub share_tokens: u64,
    pub other_types: u32,
    pub p_types: f64,
    pub p_tokens: f64,
    pub dof: u32,
    pub rare: c_int,
    pub alpha: f64,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RawHit {
    word: [c_char; 96],
    tokens: u32,
    r#ref: [c_char; 32],
}

/// One corpus word type matching the query.
#[derive(Clone, Debug)]
pub struct Hit {
    pub word: String,
    pub tokens: u32,
    pub first: String,
}

pub struct Info {
    pub types: u32,
    pub tokens: u64,
    pub frozen_ok: bool,
    pub frozen: String,
}

enum Gemstat {}

unsafe extern "C" {
    fn gemstat_open(path: *const c_char, err: *mut c_char, errlen: usize) -> *mut Gemstat;
    fn gemstat_close(g: *mut Gemstat);
    fn gemstat_get_info(g: *const Gemstat, out: *mut RawInfo);
    fn gemstat_method_name(m: c_uint) -> *const c_char;
    fn gemstat_values(text: *const c_char, out: *mut u64, words: *mut c_uint) -> c_long;
    fn gemstat_audit(g: *mut Gemstat, a: *const c_char, b: *const c_char, mask: u32, kolel: c_int,
                     out: *mut Claim) -> c_int;
    fn gemstat_matches(g: *mut Gemstat, a: *const c_char, mask: u32, kolel: c_int, out: *mut Claim,
                       hits: *mut RawHit, cap: u32) -> c_long;
    fn gemstat_base_rate(g: *mut Gemstat, mask: u32, kolel: c_int) -> f64;
}

fn text(buf: &[c_char]) -> String {
    // The backend always NUL-terminates inside the buffer (snprintf).
    unsafe { CStr::from_ptr(buf.as_ptr()) }.to_string_lossy().into_owned()
}

pub fn method_name(m: usize) -> String {
    let p = unsafe { gemstat_method_name(m as c_uint) };
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

/// Values of Hebrew text under all 16 methods; None if it is not Hebrew.
pub fn values(text_utf8: &str) -> Option<[u64; METHODS]> {
    let c = CString::new(text_utf8).ok()?;
    let mut out = [0u64; METHODS];
    let mut words: c_uint = 0;
    let n = unsafe { gemstat_values(c.as_ptr(), out.as_mut_ptr(), &mut words) };
    (n > 0).then_some(out)
}

/// Owner of one backend handle (corpus + lexicon).
pub struct Engine {
    g: *mut Gemstat,
}

impl Engine {
    pub fn open(path: &str) -> Result<Engine, String> {
        let c = CString::new(path).map_err(|e| e.to_string())?;
        let mut err = [0 as c_char; 256];
        let g = unsafe { gemstat_open(c.as_ptr(), err.as_mut_ptr(), err.len()) };
        if g.is_null() { Err(text(&err)) } else { Ok(Engine { g }) }
    }

    pub fn info(&self) -> Info {
        let mut raw = RawInfo { types: 0, tokens: 0, frozen_ok: 0, frozen: [0; 200] };
        unsafe { gemstat_get_info(self.g, &mut raw) };
        Info { types: raw.types, tokens: raw.tokens, frozen_ok: raw.frozen_ok != 0, frozen: text(&raw.frozen) }
    }

    pub fn audit(&mut self, a: &str, b: &str, mask: u32, kolel: i32) -> Option<Claim> {
        let (ca, cb) = (CString::new(a).ok()?, CString::new(b).ok()?);
        let mut out = Claim::default();
        let rc = unsafe { gemstat_audit(self.g, ca.as_ptr(), cb.as_ptr(), mask, kolel, &mut out) };
        (rc == 0).then_some(out)
    }

    pub fn matches(&mut self, a: &str, mask: u32, kolel: i32, cap: usize) -> Option<(Claim, Vec<Hit>)> {
        let ca = CString::new(a).ok()?;
        let mut out = Claim::default();
        let mut raw = vec![RawHit { word: [0; 96], tokens: 0, r#ref: [0; 32] }; cap];
        let n = unsafe { gemstat_matches(self.g, ca.as_ptr(), mask, kolel, &mut out, raw.as_mut_ptr(), cap as u32) };
        if n < 0 {
            return None;
        }
        let hits = raw[..n as usize]
            .iter()
            .map(|h| Hit { word: text(&h.word), tokens: h.tokens, first: text(&h.r#ref) })
            .collect();
        Some((out, hits))
    }
}

impl Engine {
    /// Fraction of corpus types the auditor would call "rare" under
    /// (mask, kolel): its p depends on A alone (exploratory, cached).
    pub fn base_rate(&mut self, mask: u32, kolel: i32) -> Option<f64> {
        let r = unsafe { gemstat_base_rate(self.g, mask, kolel) };
        (r >= 0.0).then_some(r)
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        unsafe { gemstat_close(self.g) };
    }
}
