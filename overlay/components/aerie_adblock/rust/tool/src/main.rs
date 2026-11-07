// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

//! Build-time tool: compile filter lists into one serialized engine.
//!
//! ```text
//! aerie_adblock_compile --out engine.dat \
//!     [--expect-block 'URL|HOST|SOURCE_HOST|TYPE|THIRD_PARTY' ...] \
//!     [--expect-allow 'URL|HOST|SOURCE_HOST|TYPE|THIRD_PARTY' ...] \
//!     [--expect-hide 'PAGE_URL|CLASSES|IDS|SELECTOR' ...] \
//!     [--expect-keep 'PAGE_URL|CLASSES|IDS|SELECTOR' ...] \
//!     list1.txt list2.txt ...
//! ```
//!
//! It builds the engine from the lists, serializes it, loads the bytes back
//! the way the browser will, and runs the `--expect-*` self-checks against
//! the reloaded engine. Any failure exits non-zero and writes nothing, so a
//! build with an empty, truncated or wrong list stops here (fail closed).

use aerie_adblock_core::{FilterEngine, RequestInfo, RequestKind};
use std::process::ExitCode;

fn kind(name: &str) -> Option<RequestKind> {
    Some(match name {
        "other" => RequestKind::Other,
        "document" => RequestKind::Document,
        "subdocument" => RequestKind::Subdocument,
        "script" => RequestKind::Script,
        "stylesheet" => RequestKind::Stylesheet,
        "image" => RequestKind::Image,
        "font" => RequestKind::Font,
        "media" => RequestKind::Media,
        "xmlhttprequest" => RequestKind::XmlHttpRequest,
        "ping" => RequestKind::Ping,
        "websocket" => RequestKind::Websocket,
        _ => return None,
    })
}

struct Probe {
    spec: String,
    expect_block: bool,
}

/// A cosmetic self-check: is `selector` hidden on the page, given the
/// whitespace-separated classes and ids it uses?
struct CosmeticProbe {
    spec: String,
    expect_hidden: bool,
}

fn run_cosmetic_probe(engine: &FilterEngine, probe: &CosmeticProbe) -> Result<(), String> {
    let parts: Vec<&str> = probe.spec.splitn(4, '|').collect();
    if parts.len() != 4 {
        return Err(format!("bad probe {:?}: want PAGE_URL|CLASSES|IDS|SELECTOR", probe.spec));
    }
    let css = format!("{}{}", engine.cosmetic_stylesheet(parts[0]), engine.generic_stylesheet(parts[0], parts[1], parts[2]));
    let rule = format!("{}{{display:none!important}}", parts[3]);
    let hidden = css.lines().any(|line| line == rule);
    if hidden != probe.expect_hidden {
        return Err(format!(
            "self-check failed: {:?} was {} but must be {}",
            probe.spec,
            if hidden { "hidden" } else { "not hidden" },
            if probe.expect_hidden { "hidden" } else { "not hidden" },
        ));
    }
    Ok(())
}

fn run_probe(engine: &FilterEngine, probe: &Probe) -> Result<(), String> {
    let parts: Vec<&str> = probe.spec.split('|').collect();
    if parts.len() != 5 {
        return Err(format!("bad probe {:?}: want URL|HOST|SOURCE_HOST|TYPE|THIRD_PARTY", probe.spec));
    }
    let kind = kind(parts[3]).ok_or_else(|| format!("bad request type in probe {:?}", probe.spec))?;
    let third_party = match parts[4] {
        "true" => true,
        "false" => false,
        _ => return Err(format!("bad THIRD_PARTY in probe {:?}", probe.spec)),
    };
    let blocked = engine.should_block(&RequestInfo {
        url: parts[0],
        hostname: parts[1],
        source_hostname: parts[2],
        kind,
        third_party,
    });
    if blocked != probe.expect_block {
        return Err(format!(
            "self-check failed: {:?} was {} but must be {}",
            probe.spec,
            if blocked { "blocked" } else { "allowed" },
            if probe.expect_block { "blocked" } else { "allowed" },
        ));
    }
    Ok(())
}

fn run() -> Result<String, String> {
    let mut out: Option<String> = None;
    let mut probes = Vec::new();
    let mut cosmetic_probes = Vec::new();
    let mut lists = Vec::new();
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--out" => out = Some(args.next().ok_or("--out needs a path")?),
            "--expect-block" => probes.push(Probe { spec: args.next().ok_or("--expect-block needs a value")?, expect_block: true }),
            "--expect-allow" => probes.push(Probe { spec: args.next().ok_or("--expect-allow needs a value")?, expect_block: false }),
            "--expect-hide" => cosmetic_probes.push(CosmeticProbe { spec: args.next().ok_or("--expect-hide needs a value")?, expect_hidden: true }),
            "--expect-keep" => cosmetic_probes.push(CosmeticProbe { spec: args.next().ok_or("--expect-keep needs a value")?, expect_hidden: false }),
            flag if flag.starts_with("--") => return Err(format!("unknown flag {flag}")),
            path => lists.push(path.to_string()),
        }
    }
    let out = out.ok_or("missing --out")?;
    if lists.is_empty() {
        return Err("no filter lists given".into());
    }
    let mut texts = Vec::new();
    for path in &lists {
        let bytes = std::fs::read(path).map_err(|e| format!("cannot read {path}: {e}"))?;
        if bytes.is_empty() {
            return Err(format!("{path} is empty"));
        }
        texts.push(bytes);
    }
    let built = FilterEngine::from_lists(texts.iter().map(|t| t.as_slice())).map_err(|e| format!("cannot build engine: {e}"))?;
    let bytes = built.serialize();
    let mut reloaded = FilterEngine::empty();
    reloaded.load_serialized(&bytes).map_err(|e| format!("serialized engine does not load back: {e}"))?;
    for probe in &probes {
        run_probe(&reloaded, probe)?;
    }
    for probe in &cosmetic_probes {
        run_cosmetic_probe(&reloaded, probe)?;
    }
    std::fs::write(&out, &bytes).map_err(|e| format!("cannot write {out}: {e}"))?;
    Ok(format!("{} bytes from {} list(s), {} self-check(s) passed -> {}", bytes.len(), lists.len(), probes.len() + cosmetic_probes.len(), out))
}

fn main() -> ExitCode {
    match run() {
        Ok(msg) => {
            println!("aerie_adblock_compile: {msg}");
            ExitCode::SUCCESS
        }
        Err(msg) => {
            eprintln!("aerie_adblock_compile: error: {msg}");
            ExitCode::FAILURE
        }
    }
}
