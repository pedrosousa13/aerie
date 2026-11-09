// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

//! Build-time tool: compile phishing and malware domain lists into one
//! serialized protection engine.
//!
//! ```text
//! aerie_protection_compile --out protection.dat --min-entries 10000 \
//!     [--expect-listed HOST ...] [--expect-unlisted HOST ...] \
//!     list1.txt list2.txt ...
//! ```
//!
//! It builds the engine, serializes it, loads the bytes back the way the
//! browser will, and runs the self-checks on the reloaded engine. A list with
//! fewer than `--min-entries` domains (an empty or truncated download) or any
//! failed check exits non-zero and writes nothing, so a bad list stops the
//! build (fail closed).

use aerie_adblock_core::protection::ProtectionList;
use std::process::ExitCode;

#[derive(Debug, Default)]
struct Args {
    out: Option<String>,
    min_entries: usize,
    listed: Vec<String>,
    unlisted: Vec<String>,
    lists: Vec<String>,
}

fn parse_args<I: Iterator<Item = String>>(mut args: I) -> Result<Args, String> {
    let mut parsed = Args::default();
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--out" => parsed.out = Some(args.next().ok_or("--out needs a path")?),
            "--min-entries" => {
                let value = args.next().ok_or("--min-entries needs a number")?;
                parsed.min_entries = value.parse().map_err(|_| format!("bad --min-entries {value:?}"))?;
            }
            "--expect-listed" => parsed.listed.push(args.next().ok_or("--expect-listed needs a host")?),
            "--expect-unlisted" => parsed.unlisted.push(args.next().ok_or("--expect-unlisted needs a host")?),
            flag if flag.starts_with("--") => return Err(format!("unknown flag {flag}")),
            path => parsed.lists.push(path.to_string()),
        }
    }
    Ok(parsed)
}

/// Build, serialize, reload and check. Returns the serialized bytes.
fn compile(texts: &[Vec<u8>], args: &Args) -> Result<(Vec<u8>, usize), String> {
    let built = ProtectionList::from_lists(texts.iter().map(|t| t.as_slice()))
        .map_err(|e| format!("cannot build protection engine: {e}"))?;
    if built.len() == 0 || built.len() < args.min_entries {
        return Err(format!(
            "the lists hold {} domain(s), at least {} are required (empty or truncated list?)",
            built.len(),
            args.min_entries.max(1)
        ));
    }
    let bytes = built.serialize();
    let mut reloaded = ProtectionList::empty();
    reloaded.load_serialized(&bytes).map_err(|e| format!("serialized engine does not load back: {e}"))?;
    if reloaded.len() != built.len() {
        return Err("serialized engine lost domains on reload".into());
    }
    for host in &args.listed {
        if !reloaded.is_listed(host) {
            return Err(format!("self-check failed: {host:?} must be listed but is not"));
        }
    }
    for host in &args.unlisted {
        if reloaded.is_listed(host) {
            return Err(format!("self-check failed: {host:?} must not be listed but is"));
        }
    }
    Ok((bytes, built.len()))
}

fn run() -> Result<String, String> {
    let args = parse_args(std::env::args().skip(1))?;
    let out = args.out.clone().ok_or("missing --out")?;
    if args.lists.is_empty() {
        return Err("no lists given".into());
    }
    let mut texts = Vec::new();
    for path in &args.lists {
        let bytes = std::fs::read(path).map_err(|e| format!("cannot read {path}: {e}"))?;
        if bytes.is_empty() {
            return Err(format!("{path} is empty"));
        }
        texts.push(bytes);
    }
    let (bytes, count) = compile(&texts, &args)?;
    std::fs::write(&out, &bytes).map_err(|e| format!("cannot write {out}: {e}"))?;
    Ok(format!(
        "{count} domain(s) from {} list(s), {} self-check(s) passed -> {out}",
        args.lists.len(),
        args.listed.len() + args.unlisted.len()
    ))
}

fn main() -> ExitCode {
    match run() {
        Ok(msg) => {
            println!("aerie_protection_compile: {msg}");
            ExitCode::SUCCESS
        }
        Err(msg) => {
            eprintln!("aerie_protection_compile: error: {msg}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(listed: &[&str], unlisted: &[&str], min: usize) -> Args {
        Args {
            min_entries: min,
            listed: listed.iter().map(|s| s.to_string()).collect(),
            unlisted: unlisted.iter().map(|s| s.to_string()).collect(),
            ..Args::default()
        }
    }

    const LIST: &[u8] = b"# header\nevil.example\nphish.example\n";

    #[test]
    fn compiles_and_passes_checks() {
        let (bytes, n) = compile(&[LIST.to_vec()], &args(&["x.evil.example"], &["good.example"], 2)).unwrap();
        assert_eq!(n, 2);
        assert!(bytes.starts_with(aerie_adblock_core::protection::SERIALIZED_MAGIC));
    }

    #[test]
    fn a_failed_listed_check_stops_the_build() {
        let err = compile(&[LIST.to_vec()], &args(&["missing.example"], &[], 1)).unwrap_err();
        assert!(err.contains("must be listed"), "{err}");
    }

    #[test]
    fn a_failed_unlisted_check_stops_the_build() {
        let err = compile(&[LIST.to_vec()], &args(&[], &["evil.example"], 1)).unwrap_err();
        assert!(err.contains("must not be listed"), "{err}");
    }

    #[test]
    fn a_truncated_or_empty_list_stops_the_build() {
        assert!(compile(&[LIST.to_vec()], &args(&[], &[], 3)).unwrap_err().contains("at least 3"));
        assert!(compile(&[b"# only comments\n".to_vec()], &args(&[], &[], 0)).is_err());
    }

    #[test]
    fn parses_flags_and_rejects_unknown_ones() {
        let ok = parse_args(
            ["--out", "o", "--min-entries", "5", "--expect-listed", "a.b", "--expect-unlisted", "c.d", "l.txt"]
                .iter()
                .map(|s| s.to_string()),
        )
        .unwrap();
        assert_eq!((ok.out.as_deref(), ok.min_entries, ok.lists.len()), (Some("o"), 5, 1));
        assert!(parse_args(["--bogus"].iter().map(|s| s.to_string())).is_err());
        assert!(parse_args(["--min-entries", "x"].iter().map(|s| s.to_string())).is_err());
    }
}
