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
//! * Network rules and plain element-hiding rules (`##selector`, `#@#`)
//!   are loaded. Scriptlets (`+js`), procedural and style-injection
//!   cosmetic rules and redirect resources are not: `sanitize_list`
//!   drops those lines, so no list can make the engine produce script.
//! * The only cosmetic output is a stylesheet of `display:none` rules,
//!   one per selector, and every selector in it passed `is_safe_selector`.
//! * Requests are described by values the browser already computed
//!   (hostnames and the third-party flag), so network matching never
//!   asks `adblock` for a registrable domain. The cosmetic functions
//!   take a page URL and `adblock` has to find its domain (for
//!   `site.*##rule` entities). The embedded resolver is off, and
//!   `adblock` panics without one, so `AerieDomainResolver` is
//!   installed first. See its documentation for the limits.

pub mod safety;

use adblock::Engine;
use adblock::lists::{FilterSet, ParseOptions, RuleTypes};
use adblock::request::Request;
use std::collections::BTreeSet;
use std::fmt;
use std::sync::Once;

/// Longest filter list text accepted, in bytes. EasyList and EasyPrivacy
/// together are well under 10 MiB. A bigger input is refused whole.
pub const MAX_LIST_BYTES: usize = 32 * 1024 * 1024;

/// Longest single line kept, in bytes. Longer lines are dropped before
/// parsing, so one hostile line cannot make the parser or a regex big.
pub const MAX_LINE_BYTES: usize = 8 * 1024;

/// Longest selector kept, in bytes.
pub const MAX_SELECTOR_BYTES: usize = 2 * 1024;

/// Most selectors in one stylesheet.
pub const MAX_COSMETIC_SELECTORS: usize = 20_000;

/// Largest stylesheet returned, in bytes.
pub const MAX_COSMETIC_CSS_BYTES: usize = 1024 * 1024;

/// Most class names, and most ids, read from a page for generic hides. The
/// rest are ignored.
pub const MAX_PAGE_TOKENS: usize = 5_000;

/// Longest class name or id read from a page, in bytes.
pub const MAX_TOKEN_BYTES: usize = 256;

/// Longest page URL the cosmetic functions look at, in bytes.
pub const MAX_URL_BYTES: usize = 8 * 1024;

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

pub(crate) fn parse_options() -> ParseOptions {
    ParseOptions {
        rule_types: RuleTypes::All,
        ..ParseOptions::default()
    }
}

/// An exception rule with no pattern (`@@`, `@@$image`, `@@*`, `@@||`)
/// matches every URL, so one such line would switch off all blocking.
/// No real list needs one, and a malformed list must not get that power.
///
/// This only knows the empty forms. `is_ignored_exception` also catches
/// the rules that match every URL by another pattern.
pub(crate) fn is_match_all_exception(line: &str) -> bool {
    let Some(rest) = line.strip_prefix("@@") else {
        return false;
    };
    let pattern = rest.split('$').next().unwrap_or("").trim();
    matches!(pattern, "" | "*" | "|" | "||" | "^")
}

/// Addresses that no real exception rule should cover: the `.invalid` top
/// level domain is reserved (RFC 2606), so no real site lives there. One
/// has a path, one has a port and a query, one has no path at all.
const PROBE_URLS: [(&str, &str); 5] = [
    (
        "https://aerie-probe-1.invalid/aerie-probe/0f3a9c71",
        "aerie-probe-1.invalid",
    ),
    (
        "http://aerie-probe-2.invalid:8080/x/y.z?q=1",
        "aerie-probe-2.invalid",
    ),
    ("https://aerie-probe-3.invalid/", "aerie-probe-3.invalid"),
    (
        "ws://aerie-probe-4.invalid/aerie-probe/socket",
        "aerie-probe-4.invalid",
    ),
    (
        "wss://aerie-probe-5.invalid/aerie-probe/socket",
        "aerie-probe-5.invalid",
    ),
];

/// The first party for the third-party probes.
const PROBE_SOURCE: &str = "aerie-probe-source.invalid";

const PROBE_KINDS: [RequestKind; 12] = [
    RequestKind::Other,
    RequestKind::Document,
    RequestKind::Subdocument,
    RequestKind::Script,
    RequestKind::Stylesheet,
    RequestKind::Image,
    RequestKind::Font,
    RequestKind::Media,
    RequestKind::XmlHttpRequest,
    RequestKind::Ping,
    RequestKind::Websocket,
    RequestKind::Object,
];

/// Does this exception rule apply to arbitrary addresses?
///
/// It tests the effect, not the pattern text: the rule is compiled alone,
/// and asked about the probe addresses above, for every request type, as
/// first party and as third party, from a first party that no `$domain=`
/// option names. A rule that applies to any of them is not scoped to a
/// site or a path, so it would switch off blocking for that type of
/// request everywhere (`@@http`, `@@.`, `@@/./`, `@@||*`, `@@*$script`).
///
/// A host-anchored rule (`@@||host...`) needs a literal first character
/// and is not tested: it is scoped by its host.
pub(crate) fn exception_matches_every_address(line: &str) -> bool {
    let Some(rest) = line.trim().strip_prefix("@@") else {
        return false;
    };
    let pattern = rest.split('$').next().unwrap_or("");
    if pattern
        .strip_prefix("||")
        .and_then(|p| p.chars().next())
        .is_some_and(|c| c.is_ascii_alphanumeric())
    {
        return false;
    }
    let mut set = FilterSet::new(false);
    set.add_filter_list(line.to_string(), parse_options());
    let engine = Engine::new_with_filter_set(set);
    for (url, host) in PROBE_URLS {
        for kind in PROBE_KINDS {
            for source in [host, PROBE_SOURCE] {
                let req = Request::preparsed(
                    url,
                    host,
                    source,
                    kind.as_filter_type(),
                    source != host,
                    "",
                );
                if engine
                    .check_network_request_subset(&req, false, true)
                    .exception
                    .is_some()
                {
                    return true;
                }
            }
        }
    }
    false
}

/// Is this line an exception rule that the engine must not apply?
pub(crate) fn is_ignored_exception(line: &str) -> bool {
    is_match_all_exception(line) || exception_matches_every_address(line)
}

/// Finds the registrable domain of a host for `adblock`'s cosmetic
/// matching. The embedded public suffix list is not compiled in (no extra
/// crate to vet), so this is a heuristic: the last two labels, or the last
/// three when the second last is a common generic label (`co`, `com`,
/// `org`...) and the last is a two letter country code (`example.co.uk`).
/// IP addresses and single labels are kept whole. The only effect of a
/// wrong answer is on entity rules (`site.*##rule`) for unusual suffixes:
/// a hide that should apply may not. It cannot make a hide apply to a site
/// it should not, because `domain##rule` matching uses the full host.
pub struct AerieDomainResolver;

impl adblock::url_parser::ResolvesDomain for AerieDomainResolver {
    fn get_host_domain(&self, host: &str) -> (usize, usize) {
        let whole = (0, host.len());
        if host.contains(':') || host.chars().all(|c| c.is_ascii_digit() || c == '.') {
            return whole;
        }
        let dots: Vec<usize> = host.match_indices('.').map(|(i, _)| i).collect();
        let Some(&last_dot) = dots.last() else {
            return whole;
        };
        let tld = &host[last_dot + 1..];
        let keep = match dots.len() {
            0 | 1 => return whole,
            n => {
                let second = &host[dots[n - 2] + 1..last_dot];
                let generic = matches!(
                    second,
                    "co" | "com" | "org" | "net" | "gov" | "edu" | "ac" | "or" | "ne" | "go"
                );
                if generic && tld.len() == 2 { 3 } else { 2 }
            }
        };
        if dots.len() < keep {
            return whole;
        }
        (dots[dots.len() - keep] + 1, host.len())
    }
}

static INSTALL_RESOLVER: Once = Once::new();

/// Install `AerieDomainResolver` for `adblock`, once per process. A
/// resolver set by someone else stays.
fn install_domain_resolver() {
    INSTALL_RESOLVER.call_once(|| {
        let _ = adblock::url_parser::set_domain_resolver(Box::new(AerieDomainResolver));
    });
}

/// Cosmetic rule markers that are not plain element hiding: HTML filtering,
/// scriptlets, procedural cosmetics and style injection, and their
/// exceptions. Lines with one of these are dropped.
const NON_PLAIN_COSMETIC_MARKERS: [&str; 6] = ["#?#", "#@?#", "#$#", "#@$#", "#%#", "#@%#"];

/// What a list line is, as far as Aerie cares.
enum LineKind<'a> {
    Network,
    /// `##` or `#@#` with the selector text that follows.
    PlainHide(&'a str),
    /// A cosmetic rule Aerie does not keep.
    OtherCosmetic,
}

fn classify_line(line: &str) -> LineKind<'_> {
    if line.starts_with('!') {
        return LineKind::Network; // a comment; the parser ignores it
    }
    let plain = [
        line.find("##").map(|i| (i, 2)),
        line.find("#@#").map(|i| (i, 3)),
    ]
    .into_iter()
    .flatten()
    .min_by_key(|(i, _)| *i);
    let other = NON_PLAIN_COSMETIC_MARKERS
        .iter()
        .filter_map(|m| line.find(m))
        .min();
    match (plain, other) {
        (Some((i, len)), None) => LineKind::PlainHide(&line[i + len..]),
        (Some((i, len)), Some(o)) => {
            if i < o {
                LineKind::PlainHide(&line[i + len..])
            } else {
                LineKind::OtherCosmetic
            }
        }
        (None, Some(_)) => LineKind::OtherCosmetic,
        (None, None) => LineKind::Network,
    }
}

/// Is this CSS selector text fit to put in a stylesheet that Aerie writes?
///
/// Strict on purpose. The text comes from a filter list, and a stylesheet
/// is built from it. Allowed: plain selector text with balanced brackets,
/// parentheses and quotes (quotes only inside them). Refused: anything
/// that could end the rule or start another (`{`, `}`, `;`), comments,
/// backslash escapes, `<`, `@`-rules, `url(`, control characters, the
/// scriptlet and HTML-filter prefixes (`+js(`, `^`), the action pseudos
/// `:style(` and `:remove(`, and anything over `MAX_SELECTOR_BYTES`.
pub fn is_safe_selector(selector: &str) -> bool {
    if selector.is_empty() || selector.len() > MAX_SELECTOR_BYTES || selector.trim().is_empty() {
        return false;
    }
    if selector.starts_with(['+', '^', '@']) {
        return false;
    }
    let lower = selector.to_ascii_lowercase();
    if lower.contains("url(")
        || lower.contains(":style(")
        || lower.contains(":remove(")
        || lower.contains("/*")
        || lower.contains("*/")
    {
        return false;
    }
    let mut quote: Option<char> = None;
    let mut brackets = 0usize;
    let mut parens = 0usize;
    for c in selector.chars() {
        if c.is_control() || matches!(c, '{' | '}' | ';' | '<' | '\\') {
            return false;
        }
        if let Some(q) = quote {
            if c == q {
                quote = None;
            }
            continue;
        }
        match c {
            '"' | '\'' => {
                if brackets == 0 && parens == 0 {
                    return false;
                }
                quote = Some(c);
            }
            '@' => return false,
            '[' => brackets += 1,
            ']' => match brackets.checked_sub(1) {
                Some(n) => brackets = n,
                None => return false,
            },
            '(' => parens += 1,
            ')' => match parens.checked_sub(1) {
                Some(n) => parens = n,
                None => return false,
            },
            _ => {}
        }
    }
    quote.is_none() && brackets == 0 && parens == 0
}

/// The stylesheet that hides `selectors`: one `selector{display:none!important}`
/// line each, sorted, without duplicates. A selector that fails
/// `is_safe_selector` is skipped. Stops at `MAX_COSMETIC_SELECTORS` rules
/// or `MAX_COSMETIC_CSS_BYTES` bytes. One rule per selector, because a
/// selector that a browser cannot parse voids a whole comma list.
pub fn hide_stylesheet<S: AsRef<str>>(selectors: impl IntoIterator<Item = S>) -> String {
    const TAIL: &str = "{display:none!important}\n";
    let safe: BTreeSet<String> = selectors
        .into_iter()
        .filter(|s| is_safe_selector(s.as_ref()))
        .map(|s| s.as_ref().to_string())
        .collect();
    let mut css = String::new();
    for (count, selector) in safe.iter().enumerate() {
        if count >= MAX_COSMETIC_SELECTORS
            || css.len() + selector.len() + TAIL.len() > MAX_COSMETIC_CSS_BYTES
        {
            break;
        }
        css.push_str(selector);
        css.push_str(TAIL);
    }
    css
}

/// Whitespace-separated class names or ids from a page, as a set: no
/// control characters, none over `MAX_TOKEN_BYTES`, at most `MAX_PAGE_TOKENS`.
fn page_tokens(text: &str) -> BTreeSet<&str> {
    text.split_ascii_whitespace()
        .filter(|t| t.len() <= MAX_TOKEN_BYTES && !t.chars().any(char::is_control))
        .take(MAX_PAGE_TOKENS)
        .collect()
}

fn is_web_url(url: &str) -> bool {
    url.len() <= MAX_URL_BYTES
        && ["http://", "https://"].iter().any(|p| {
            url.get(..p.len())
                .is_some_and(|s| s.eq_ignore_ascii_case(p))
        })
}

/// The result of `sanitize_list_report`.
#[derive(Debug, PartialEq, Eq)]
pub struct SanitizedList {
    /// The list text that is safe to parse.
    pub text: String,
    /// The exception rules that were left out because they match every
    /// address (at most `MAX_REPORTED_RULES`, each cut to `MAX_LINE_BYTES`).
    pub ignored_exceptions: Vec<String>,
}

/// Most ignored rules kept in a report, so a hostile list cannot make the
/// report big.
pub const MAX_REPORTED_RULES: usize = 20;

/// Make list bytes safe to parse: lossy UTF-8, NULs and carriage returns
/// gone, over-long lines and exception rules that match every address
/// dropped, and every cosmetic rule that is not a plain hide with a safe
/// selector dropped. Never fails and never panics.
pub fn sanitize_list(bytes: &[u8]) -> String {
    sanitize_list_report(bytes).text
}

/// Like `sanitize_list`, and also says which exception rules were ignored,
/// so the caller can log them.
pub fn sanitize_list_report(bytes: &[u8]) -> SanitizedList {
    let text = String::from_utf8_lossy(bytes);
    let mut out = String::with_capacity(text.len());
    let mut ignored_exceptions = Vec::new();
    for line in text.split('\n') {
        let line = line.trim_end_matches('\r');
        if line.len() > MAX_LINE_BYTES || line.contains('\0') {
            continue;
        }
        if is_ignored_exception(line) {
            if ignored_exceptions.len() < MAX_REPORTED_RULES {
                ignored_exceptions.push(line.to_string());
            }
            continue;
        }
        match classify_line(line) {
            LineKind::Network => {}
            LineKind::PlainHide(selector) if is_safe_selector(selector) => {}
            LineKind::PlainHide(_) | LineKind::OtherCosmetic => continue,
        }
        out.push_str(line);
        out.push('\n');
    }
    SanitizedList {
        text: out,
        ignored_exceptions,
    }
}

impl FilterEngine {
    /// An engine with no rules. It blocks nothing.
    pub fn empty() -> Self {
        Self::from_filter_set(FilterSet::new(false))
    }

    fn from_filter_set(set: FilterSet) -> Self {
        install_domain_resolver();
        FilterEngine {
            engine: Engine::new_with_filter_set(set),
        }
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

    /// The stylesheet to inject into a page as soon as it commits: rules
    /// for this site, and the generic rules that do not start with a class
    /// or id. Empty for a URL that is not http(s). See `hide_stylesheet`.
    pub fn cosmetic_stylesheet(&self, page_url: &str) -> String {
        if !is_web_url(page_url) {
            return String::new();
        }
        install_domain_resolver();
        let resources = self.engine.url_cosmetic_resources(page_url);
        hide_stylesheet(resources.hide_selectors)
    }

    /// The generic rules for the classes and ids that a page uses (each a
    /// whitespace-separated list). Empty when the site has a `$generichide`
    /// exception or the URL is not http(s). Pass the page's classes and ids
    /// when it has loaded, and again when more appear.
    pub fn generic_stylesheet(&self, page_url: &str, classes: &str, ids: &str) -> String {
        if !is_web_url(page_url) {
            return String::new();
        }
        install_domain_resolver();
        let resources = self.engine.url_cosmetic_resources(page_url);
        if resources.generichide {
            return String::new();
        }
        let selectors = self.engine.hidden_class_id_selectors(
            page_tokens(classes),
            page_tokens(ids),
            &resources.exceptions,
        );
        hide_stylesheet(selectors)
    }
}

mod lists;
pub use lists::{ListCheck, ListError, check_list};

#[cfg(test)]
mod tests;
