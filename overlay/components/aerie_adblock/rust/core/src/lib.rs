// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

//! Aerie's filter engine core: a thin, panic-free layer over `adblock`
//! (brave/adblock-rust, MPL-2.0, used unmodified).
//!
//! This crate has no C++ in it. `aerie_adblock_ffi` puts the cxx bridge on
//! top, and `aerie_adblock_compile` (the build-time tool) uses it directly.
//!
//! Rules for this crate:
//!
//! * Nothing here may panic on any input. In Chromium, Rust is built with
//!   `panic=abort`, so a panic would take the browser down. Lists and
//!   serialized engines are treated as untrusted bytes.
//! * Only network rules are loaded. Cosmetic filters, scriptlets and
//!   redirect resources are out of scope for the first Filter Engine.
//! * Requests are described by values the browser already computed
//!   (hostnames and the third-party flag). `adblock`'s own domain
//!   resolver is never used.

use adblock::Engine;
use adblock::lists::{FilterSet, ParseOptions, RuleTypes};
use adblock::request::Request;
use std::fmt;

/// Longest filter list text accepted, in bytes. EasyList and EasyPrivacy
/// together are well under 10 MiB. A bigger input is refused whole.
pub const MAX_LIST_BYTES: usize = 32 * 1024 * 1024;

/// Longest single line kept, in bytes. Longer lines are dropped before
/// parsing, so one hostile line cannot make the parser or a regex big.
pub const MAX_LINE_BYTES: usize = 8 * 1024;

/// Largest serialized engine accepted, in bytes.
pub const MAX_SERIALIZED_BYTES: usize = 256 * 1024 * 1024;

/// The kind of resource a request fetches, as the filter syntax names it.
///
/// `Websocket` exists so the mapping is complete. Aerie does not block
/// WebSockets in the first Filter Engine, so the browser never asks about it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RequestKind {
    Other,
    Document,
    Subdocument,
    Script,
    Stylesheet,
    Image,
    Font,
    Media,
    XmlHttpRequest,
    Ping,
    Websocket,
    Object,
}

impl RequestKind {
    /// The name `adblock` expects for the `$type` options.
    pub fn as_filter_type(self) -> &'static str {
        match self {
            RequestKind::Other => "other",
            RequestKind::Document => "document",
            RequestKind::Subdocument => "subdocument",
            RequestKind::Script => "script",
            RequestKind::Stylesheet => "stylesheet",
            RequestKind::Image => "image",
            RequestKind::Font => "font",
            RequestKind::Media => "media",
            RequestKind::XmlHttpRequest => "xmlhttprequest",
            RequestKind::Ping => "ping",
            RequestKind::Websocket => "websocket",
            RequestKind::Object => "object",
        }
    }
}

/// What the engine needs to know about one request.
#[derive(Clone, Copy, Debug)]
pub struct RequestInfo<'a> {
    /// The full request URL, as `GURL::spec()` gives it.
    pub url: &'a str,
    /// Lowercase host of `url`.
    pub hostname: &'a str,
    /// Lowercase host of the first party (the initiator), or "" if unknown.
    pub source_hostname: &'a str,
    pub kind: RequestKind,
    /// True when the request and the first party are on different sites.
    /// The browser decides this with its own public suffix list.
    pub third_party: bool,
}

#[derive(Debug, PartialEq, Eq)]
pub enum LoadError {
    /// The input is bigger than the limit for its kind.
    TooLarge,
    /// The bytes are not a serialized engine this build can read.
    BadEngine,
}

impl fmt::Display for LoadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LoadError::TooLarge => f.write_str("input is larger than the allowed limit"),
            LoadError::BadEngine => f.write_str("not a valid serialized engine"),
        }
    }
}

impl std::error::Error for LoadError {}

/// One engine: all lists compiled together.
pub struct FilterEngine {
    engine: Engine,
}

fn parse_options() -> ParseOptions {
    ParseOptions { rule_types: RuleTypes::NetworkOnly, ..ParseOptions::default() }
}

/// An exception rule with no pattern (`@@`, `@@$image`, `@@*`, `@@||`)
/// matches every URL, so one such line would switch off all blocking.
/// No real list needs one, and a malformed list must not get that power.
fn is_match_all_exception(line: &str) -> bool {
    let Some(rest) = line.strip_prefix("@@") else {
        return false;
    };
    let pattern = rest.split('$').next().unwrap_or("").trim();
    matches!(pattern, "" | "*" | "|" | "||" | "^")
}

/// Make list bytes safe to parse: lossy UTF-8, NULs and carriage returns
/// gone, over-long lines and match-all exceptions dropped. Never fails and
/// never panics.
pub fn sanitize_list(bytes: &[u8]) -> String {
    let text = String::from_utf8_lossy(bytes);
    let mut out = String::with_capacity(text.len());
    for line in text.split('\n') {
        let line = line.trim_end_matches('\r');
        if line.len() > MAX_LINE_BYTES || line.contains('\0') || is_match_all_exception(line) {
            continue;
        }
        out.push_str(line);
        out.push('\n');
    }
    out
}

impl FilterEngine {
    /// An engine with no rules. It blocks nothing.
    pub fn empty() -> Self {
        Self::from_filter_set(FilterSet::new(false))
    }

    fn from_filter_set(set: FilterSet) -> Self {
        FilterEngine { engine: Engine::new_with_filter_set(set) }
    }

    /// Compile filter lists (EasyList syntax) into one engine. Lines that
    /// do not parse are skipped, as `adblock` does. A list over
    /// `MAX_LIST_BYTES` is an error and nothing is built.
    pub fn from_lists<'a>(lists: impl IntoIterator<Item = &'a [u8]>) -> Result<Self, LoadError> {
        let mut set = FilterSet::new(false);
        for list in lists {
            if list.len() > MAX_LIST_BYTES {
                return Err(LoadError::TooLarge);
            }
            set.add_filter_list(sanitize_list(list), parse_options());
        }
        Ok(Self::from_filter_set(set))
    }

    /// Load a serialized engine, as `serialize` wrote it. On error the
    /// receiver is left as it was.
    pub fn load_serialized(&mut self, bytes: &[u8]) -> Result<(), LoadError> {
        if bytes.len() > MAX_SERIALIZED_BYTES {
            return Err(LoadError::TooLarge);
        }
        let mut next = Engine::default();
        next.deserialize(bytes).map_err(|_| LoadError::BadEngine)?;
        self.engine = next;
        Ok(())
    }

    /// The compact form that `load_serialized` reads. Only valid for the
    /// same version of this crate and of `adblock`.
    pub fn serialize(&self) -> Vec<u8> {
        self.engine.serialize()
    }

    /// Should this request be blocked? Exception rules (`@@`) win over
    /// ordinary block rules, and `$important` rules win over exceptions.
    pub fn should_block(&self, request: &RequestInfo<'_>) -> bool {
        if request.url.is_empty() || request.hostname.is_empty() {
            return false;
        }
        let req = Request::preparsed(
            request.url,
            request.hostname,
            request.source_hostname,
            request.kind.as_filter_type(),
            request.third_party,
            "",
        );
        self.engine.check_network_request(&req).should_block()
    }
}

#[cfg(test)]
mod tests;
