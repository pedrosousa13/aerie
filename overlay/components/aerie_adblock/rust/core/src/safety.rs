// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

//! The protection category of the Filter Engine: phishing and malware domains.
//!
//! These lists (phishing-filter, URLhaus) are lists of host names, not of URL
//! rules, so the category is a set of domains instead of adblock-rust rules. A
//! listed domain covers all its subdomains. The same rules as for the ad
//! engine apply: nothing here panics on any input, lists and serialized data
//! are untrusted bytes, and every limit fails closed (the build stops).
//!
//! Entry formats accepted: one domain per line, or hosts format
//! (`0.0.0.0 domain`, `127.0.0.1 domain`). `#` starts a comment.

use crate::{LoadError, MAX_LINE_BYTES, MAX_LIST_BYTES, MAX_SERIALIZED_BYTES};
use std::collections::HashSet;

/// Most domains one engine holds. phishing-filter has about 20 000; a list
/// that has a hundred times that is not the list the build pinned.
pub const MAX_ENTRIES: usize = 2_000_000;

/// First line of the serialized form. It names the format so that bytes of
/// another kind are refused.
pub const SERIALIZED_MAGIC: &[u8] = b"AERIE-PROTECTION-1\n";

const MAX_NAME_BYTES: usize = 253;
const MAX_LABEL_BYTES: usize = 63;

/// Normalize one list line to a domain, or `None` if the line holds none.
///
/// Rejects anything that is not a plain ASCII host name with at least two
/// labels: URLs, paths, ports, IP addresses and single labels. A single label
/// would be a whole TLD, and one bad line must not block `.com`.
pub fn parse_domain_line(line: &str) -> Option<String> {
    let line = line.split('#').next().unwrap_or("").trim();
    let mut tokens = line.split_ascii_whitespace();
    let first = tokens.next()?;
    let candidate = match first {
        "0.0.0.0" | "127.0.0.1" => tokens.next()?,
        other => other,
    };
    if tokens.next().is_some() {
        return None;
    }
    normalize_host(candidate, true)
}

fn normalize_host(raw: &str, allow_wildcard: bool) -> Option<String> {
    let mut name = raw.to_ascii_lowercase();
    if allow_wildcard {
        if let Some(rest) = name.strip_prefix("*.") {
            name = rest.to_string();
        }
    }
    let name = name.strip_suffix('.').unwrap_or(&name);
    if name.is_empty() || name.len() > MAX_NAME_BYTES {
        return None;
    }
    let mut labels = 0;
    let mut last = "";
    for label in name.split('.') {
        let ok = !label.is_empty()
            && label.len() <= MAX_LABEL_BYTES
            && !label.starts_with('-')
            && !label.ends_with('-')
            && label.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-' || b == b'_');
        if !ok {
            return None;
        }
        labels += 1;
        last = label;
    }
    // A real TLD is never all digits, so this also drops IPv4 addresses.
    if labels < 2 || last.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    Some(name.to_string())
}

/// A set of listed domains.
pub struct ProtectionList {
    domains: HashSet<Box<str>>,
}

impl ProtectionList {
    /// A list with no domains. It lists nothing.
    pub fn empty() -> Self {
        ProtectionList { domains: HashSet::new() }
    }

    /// Number of listed domains.
    pub fn len(&self) -> usize {
        self.domains.len()
    }

    pub fn is_empty(&self) -> bool {
        self.domains.is_empty()
    }

    /// Merge lists (domain per line, or hosts format). Lines that do not
    /// parse and lines over `MAX_LINE_BYTES` are skipped. A list over
    /// `MAX_LIST_BYTES`, or more than `MAX_ENTRIES` domains, is an error.
    pub fn from_lists<'a>(lists: impl IntoIterator<Item = &'a [u8]>) -> Result<Self, LoadError> {
        let mut domains: HashSet<Box<str>> = HashSet::new();
        for list in lists {
            if list.len() > MAX_LIST_BYTES {
                return Err(LoadError::TooLarge);
            }
            let text = String::from_utf8_lossy(list);
            for line in text.split('\n') {
                if line.len() > MAX_LINE_BYTES {
                    continue;
                }
                if let Some(domain) = parse_domain_line(line) {
                    domains.insert(domain.into_boxed_str());
                    if domains.len() > MAX_ENTRIES {
                        return Err(LoadError::TooLarge);
                    }
                }
            }
        }
        Ok(ProtectionList { domains })
    }

    /// Load the form `serialize` wrote. Every line is checked again: the
    /// bytes are not trusted. On error the receiver is left as it was.
    pub fn load_serialized(&mut self, bytes: &[u8]) -> Result<(), LoadError> {
        if bytes.len() > MAX_SERIALIZED_BYTES {
            return Err(LoadError::TooLarge);
        }
        let body = bytes.strip_prefix(SERIALIZED_MAGIC).ok_or(LoadError::BadEngine)?;
        let text = std::str::from_utf8(body).map_err(|_| LoadError::BadEngine)?;
        let mut domains: HashSet<Box<str>> = HashSet::new();
        for line in text.lines() {
            // Stored names are already normal: a line that changes when
            // normalized, or that is not a name at all, means damage.
            match normalize_host(line, false) {
                Some(name) if name == line => {
                    domains.insert(name.into_boxed_str());
                }
                _ => return Err(LoadError::BadEngine),
            }
            if domains.len() > MAX_ENTRIES {
                return Err(LoadError::TooLarge);
            }
        }
        self.domains = domains;
        Ok(())
    }

    /// Sorted, one domain per line, after `SERIALIZED_MAGIC`. Stable, so the
    /// same lists always give the same bytes.
    pub fn serialize(&self) -> Vec<u8> {
        let mut names: Vec<&str> = self.domains.iter().map(|d| &**d).collect();
        names.sort_unstable();
        let mut out = Vec::with_capacity(SERIALIZED_MAGIC.len() + names.iter().map(|n| n.len() + 1).sum::<usize>());
        out.extend_from_slice(SERIALIZED_MAGIC);
        for name in names {
            out.extend_from_slice(name.as_bytes());
            out.push(b'\n');
        }
        out
    }

    /// Is `host` a listed domain, or a subdomain of one? `host` is a host
    /// name as `GURL::host()` gives it. Anything that is not a plain host
    /// name (empty, IP literal, path, spaces) is not listed.
    pub fn is_listed(&self, host: &str) -> bool {
        let Some(host) = normalize_host(host, false) else {
            return false;
        };
        let mut rest: &str = &host;
        loop {
            if self.domains.contains(rest) {
                return true;
            }
            match rest.split_once('.') {
                // Stop before a lone TLD: entries have two labels or more.
                Some((_, parent)) if parent.contains('.') => rest = parent,
                _ => return false,
            }
        }
    }
}

#[cfg(test)]
#[path = "safety_tests.rs"]
mod tests;
