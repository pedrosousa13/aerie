// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

//! Checking lists that came from outside the build (issue #10).
//!
//! A list the user adds, or a newer copy of a list that a publisher serves,
//! is untrusted bytes. `check_list` decides whether the bytes are a filter
//! list at all, inside a time budget, before anything is replaced. The caller
//! keeps its working copy when a check fails, so a bad download never
//! switches blocking off.
//!
//! `adblock` accepts any text as filter lines (an HTML error page is a pile of
//! patterns), so "does it parse" is not enough. A list passes when it is
//! text (UTF-8, no NUL), does not start like an HTML page, and has at least
//! one network rule.

use crate::{
    FilterEngine, LoadError, MAX_LINE_BYTES, MAX_LIST_BYTES, is_match_all_exception, parse_options,
};
use adblock::lists::{ParsedLine, parse_filter, read_list_metadata};
use std::time::{Duration, Instant};

/// Longest list title kept, in bytes.
const MAX_TITLE_BYTES: usize = 100;

/// How many lines are parsed between two looks at the clock.
const LINES_PER_TIME_CHECK: usize = 256;

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ListError {
    /// Bigger than `MAX_LIST_BYTES`.
    TooLarge,
    /// The parse used up its time budget.
    Timeout,
    /// Not UTF-8 text, or it has a NUL byte (gzip and images land here).
    NotText,
    /// Starts like an HTML page (usually a server's error page).
    LooksLikeHtml,
    /// Text, but not one network rule in it.
    NoRules,
}

impl std::fmt::Display for ListError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            ListError::TooLarge => "the list is larger than the allowed limit",
            ListError::Timeout => "the list took too long to parse",
            ListError::NotText => "the list is not text",
            ListError::LooksLikeHtml => "the list looks like a web page, not a filter list",
            ListError::NoRules => "the list has no blocking rules",
        })
    }
}

impl std::error::Error for ListError {}

/// What a good list contained.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct ListCheck {
    /// Lines that parsed as network (blocking or exception) rules.
    pub network_rules: usize,
    /// The list's own `! Title:` line, cut to a short length.
    pub title: Option<String>,
}

fn looks_like_html(text: &str) -> bool {
    text.lines()
        .map(str::trim)
        .find(|l| !l.is_empty())
        .is_some_and(|l| l.starts_with('<'))
}

fn cut_title(title: &str) -> String {
    let mut end = title.len().min(MAX_TITLE_BYTES);
    while !title.is_char_boundary(end) {
        end -= 1;
    }
    title[..end].trim().to_string()
}

/// Decide whether `bytes` is a usable filter list. Never panics. The parse
/// stops with `Timeout` once `budget` has passed; the clock is looked at every
/// few hundred lines, so a list overruns by a few milliseconds at most.
pub fn check_list(bytes: &[u8], budget: Duration) -> Result<ListCheck, ListError> {
    check_list_until(bytes, Instant::now() + budget)
}

fn check_list_until(bytes: &[u8], deadline: Instant) -> Result<ListCheck, ListError> {
    if bytes.len() > MAX_LIST_BYTES {
        return Err(ListError::TooLarge);
    }
    let text = std::str::from_utf8(bytes).map_err(|_| ListError::NotText)?;
    if text.contains('\0') {
        return Err(ListError::NotText);
    }
    if looks_like_html(text) {
        return Err(ListError::LooksLikeHtml);
    }
    let mut network_rules = 0;
    for (i, line) in text.lines().enumerate() {
        if i % LINES_PER_TIME_CHECK == 0 && Instant::now() >= deadline {
            return Err(ListError::Timeout);
        }
        if line.len() > MAX_LINE_BYTES || is_match_all_exception(line) {
            continue;
        }
        if let Ok(ParsedLine::Network(_)) = parse_filter(line, false, parse_options()) {
            network_rules += 1;
        }
    }
    if network_rules == 0 {
        return Err(ListError::NoRules);
    }
    let title = read_list_metadata(text)
        .title
        .map(|t| cut_title(&t))
        .filter(|t| !t.is_empty());
    Ok(ListCheck {
        network_rules,
        title,
    })
}

impl FilterEngine {
    /// Check every list, then compile them all into one engine. One `budget`
    /// is shared by all the lists. On a failure nothing is built and the error
    /// carries the index of the list that failed.
    pub fn from_checked_lists<'a>(
        lists: impl IntoIterator<Item = &'a [u8]>,
        budget: Duration,
    ) -> Result<(Self, Vec<ListCheck>), (usize, ListError)> {
        let deadline = Instant::now() + budget;
        let mut checks = Vec::new();
        let mut kept: Vec<&[u8]> = Vec::new();
        for (index, list) in lists.into_iter().enumerate() {
            checks.push(check_list_until(list, deadline).map_err(|e| (index, e))?);
            kept.push(list);
        }
        let engine = Self::from_lists(kept).map_err(|e| {
            debug_assert_eq!(e, LoadError::TooLarge);
            (0, ListError::TooLarge)
        })?;
        Ok((engine, checks))
    }
}

#[cfg(test)]
#[path = "lists_tests.rs"]
mod lists_tests;
