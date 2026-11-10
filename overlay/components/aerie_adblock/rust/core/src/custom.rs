// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

//! The user's own filter rules (issue #107): the text of the textarea in the
//! Guard section of Settings.
//!
//! The text is untrusted, so it is bounded and parsed here, in the browser
//! process, before anything is applied. `analyze_custom_rules` refuses a text
//! over `MAX_CUSTOM_BYTES` or `MAX_CUSTOM_LINES` whole, drops a line over
//! `MAX_CUSTOM_LINE_BYTES`, and says for every other line it drops or ignores
//! which line it was and why, so the page can show the reason. What it drops is
//! what `sanitize_list` drops (scriptlets, procedural, HTML and style rules,
//! unsafe selectors, exception rules that match every address; see ADR 0002),
//! with a reason for each, plus lines the parser does not accept. The text
//! that is saved and built into the engine goes through `sanitize_list`
//! again (`FilterEngine::from_checked_lists_with`), so the engine never
//! depends on this report being right.
//!
//! Nothing here panics on any input.

use crate::lists::check_list_with;
use crate::{
    LineKind, MAX_REPORTED_RULES, classify_line, is_safe_selector, parse_options,
    sanitize_list_report_with,
};
use adblock::lists::{ParsedLine, parse_filter};
use std::time::{Duration, Instant};

/// Largest text accepted, in bytes (256 KiB).
pub const MAX_CUSTOM_BYTES: usize = 256 * 1024;

/// Most lines accepted.
pub const MAX_CUSTOM_LINES: usize = 10_000;

/// Longest line kept, in bytes (4 KiB). A longer line is dropped, by the
/// engine path and by the report alike: both use this one limit.
pub const MAX_CUSTOM_LINE_BYTES: usize = 4 * 1024;

/// Most issues a report carries. The counts are always complete.
pub const MAX_CUSTOM_ISSUES: usize = 100;

/// Longest rule text kept in an issue, in bytes.
pub const MAX_REPORTED_RULE_BYTES: usize = 200;

/// How many lines are handled between two looks at the clock. A broad
/// exception rule costs a probe each, so this is small.
const LINES_PER_TIME_CHECK: usize = 16;

/// Why the whole text was refused. Nothing is kept or applied.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum CustomRulesError {
    /// More than `MAX_CUSTOM_BYTES`.
    TooLarge,
    /// More than `MAX_CUSTOM_LINES` lines.
    TooManyLines,
    /// Not UTF-8.
    NotText,
    /// The parse used up its time budget.
    Timeout,
}

impl std::fmt::Display for CustomRulesError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            CustomRulesError::TooLarge => "the rules are larger than the allowed limit",
            CustomRulesError::TooManyLines => "the rules have more lines than the allowed limit",
            CustomRulesError::NotText => "the rules are not text",
            CustomRulesError::Timeout => "the rules took too long to parse",
        })
    }
}

impl std::error::Error for CustomRulesError {}

/// Why a line is not in the engine.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum RuleReason {
    /// A scriptlet rule (`+js(...)`, `#%#`). Aerie never runs them.
    Scriptlet,
    /// An HTML filtering rule (`##^`, `$$`).
    HtmlFilter,
    /// A procedural cosmetic rule (`#?#`, `:has-text()`, `:xpath()`, ...).
    Procedural,
    /// A style or snippet injection rule (`#$#`).
    StyleInjection,
    /// A hide rule whose selector is not safe to put in a stylesheet.
    UnsafeSelector,
    /// A line longer than `MAX_CUSTOM_LINE_BYTES`.
    TooLong,
    /// A line with a NUL byte.
    ContainsNul,
    /// An exception rule that matches every address. Ignored, not applied.
    MatchesEveryAddress,
    /// The filter parser does not accept the line.
    Unparseable,
}

impl RuleReason {
    /// True for a rule that is left out because it is too broad, not because
    /// it is a kind Aerie does not run or cannot read.
    pub fn is_ignored(self) -> bool {
        self == RuleReason::MatchesEveryAddress
    }
}

/// One line that is not in the engine.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct RuleIssue {
    /// 1-based line number in the text the user saved.
    pub line: usize,
    pub reason: RuleReason,
    /// The line, cut to `MAX_REPORTED_RULE_BYTES` on a character boundary.
    /// Untrusted text: show it as text, never as markup.
    pub rule: String,
}

/// What a good text contained.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct CustomRules {
    /// The text without the lines that were dropped or ignored.
    pub text: String,
    /// Lines that parsed as network (blocking or exception) rules.
    pub network_rules: usize,
    /// Lines that are plain element hiding rules with a safe selector.
    pub hide_rules: usize,
    /// Lines left out because the rule is too broad (`is_ignored`).
    pub ignored_exceptions: usize,
    /// Lines dropped for any other reason.
    pub dropped: usize,
    /// The first `MAX_CUSTOM_ISSUES` issues, in line order.
    pub issues: Vec<RuleIssue>,
    /// All issues; at least `issues.len()`.
    pub issue_count: usize,
}

fn cut_rule(line: &str) -> String {
    let mut end = line.len().min(MAX_REPORTED_RULE_BYTES);
    while !line.is_char_boundary(end) {
        end -= 1;
    }
    line[..end].to_string()
}

fn reason_for_other_cosmetic(line: &str) -> RuleReason {
    let at = |m: &str| line.find(m).unwrap_or(usize::MAX);
    let scriptlet = at("#%#").min(at("#@%#"));
    let style = at("#$#").min(at("#@$#"));
    let procedural = at("#?#").min(at("#@?#"));
    if scriptlet <= style && scriptlet <= procedural {
        RuleReason::Scriptlet
    } else if style <= procedural {
        RuleReason::StyleInjection
    } else {
        RuleReason::Procedural
    }
}

fn reason_for_selector(selector: &str) -> Option<RuleReason> {
    if selector.starts_with('+') {
        return Some(RuleReason::Scriptlet);
    }
    if selector.starts_with('^') {
        return Some(RuleReason::HtmlFilter);
    }
    if crate::PROCEDURAL_MARKERS
        .iter()
        .any(|m| selector.contains(m))
        || selector.contains(":remove(")
    {
        return Some(RuleReason::Procedural);
    }
    if !is_safe_selector(selector) {
        return Some(RuleReason::UnsafeSelector);
    }
    None
}

enum Verdict {
    Keep,
    KeepNetwork,
    KeepHide,
    Drop(RuleReason),
}

/// Judge one line. `sanitize_list_report_with` (the function the engine path
/// uses, with the same line limit) decides whether the line stays; the reasons
/// below only name why it did not. A line it keeps is counted with
/// `check_list_with`, so the report cannot say a rule counts that the engine
/// would skip.
fn judge(line: &str, deadline: Instant) -> Verdict {
    let trimmed = line.trim();
    if trimmed.is_empty() || trimmed.starts_with('!') {
        return Verdict::Keep;
    }
    // A header such as `[Adblock Plus 2.0]` that a pasted list starts with.
    if trimmed.starts_with('[') && trimmed.ends_with(']') {
        return Verdict::Keep;
    }
    let kept = sanitize_list_report_with(line.as_bytes(), MAX_CUSTOM_LINE_BYTES);
    if kept.text.is_empty() {
        return Verdict::Drop(reason_for_dropped(
            line,
            !kept.ignored_exceptions.is_empty(),
        ));
    }
    match classify_line(line) {
        // Cannot be: `sanitize` drops these. Do not claim they are kept.
        LineKind::OtherCosmetic => Verdict::Drop(reason_for_other_cosmetic(line)),
        LineKind::PlainHide(_) => match parse_filter(line, false, parse_options()) {
            Ok(ParsedLine::Cosmetic(_)) => Verdict::KeepHide,
            _ => Verdict::Drop(RuleReason::Unparseable),
        },
        LineKind::Network => {
            if line.contains("$$") {
                return Verdict::Drop(RuleReason::HtmlFilter);
            }
            match check_list_with(line.as_bytes(), deadline, false, MAX_CUSTOM_LINE_BYTES) {
                Ok(check) if check.network_rules == 1 => Verdict::KeepNetwork,
                _ => Verdict::Drop(RuleReason::Unparseable),
            }
        }
    }
}

/// The reason `sanitize_list` dropped a line.
fn reason_for_dropped(line: &str, ignored_exception: bool) -> RuleReason {
    if line.len() > MAX_CUSTOM_LINE_BYTES {
        return RuleReason::TooLong;
    }
    if line.contains('\0') {
        return RuleReason::ContainsNul;
    }
    if ignored_exception {
        return RuleReason::MatchesEveryAddress;
    }
    match classify_line(line) {
        LineKind::OtherCosmetic => reason_for_other_cosmetic(line),
        LineKind::PlainHide(selector) => {
            reason_for_selector(selector).unwrap_or(RuleReason::UnsafeSelector)
        }
        LineKind::Network => RuleReason::Unparseable,
    }
}

/// Check, bound and sort the user's own rules. Never panics. `budget` bounds
/// the time; the clock is looked at every few lines.
pub fn analyze_custom_rules(
    bytes: &[u8],
    budget: Duration,
) -> Result<CustomRules, CustomRulesError> {
    let deadline = Instant::now() + budget;
    if bytes.len() > MAX_CUSTOM_BYTES {
        return Err(CustomRulesError::TooLarge);
    }
    let text = std::str::from_utf8(bytes).map_err(|_| CustomRulesError::NotText)?;
    // `split('\n')` gives one empty piece after a final newline: not a line.
    let line_count = text.split('\n').count() - usize::from(text.ends_with('\n'));
    if line_count > MAX_CUSTOM_LINES {
        return Err(CustomRulesError::TooManyLines);
    }
    let mut out = CustomRules {
        text: String::with_capacity(text.len()),
        network_rules: 0,
        hide_rules: 0,
        ignored_exceptions: 0,
        dropped: 0,
        issues: Vec::new(),
        issue_count: 0,
    };
    for (index, line) in text.split('\n').take(line_count).enumerate() {
        if index % LINES_PER_TIME_CHECK == 0 && Instant::now() >= deadline {
            return Err(CustomRulesError::Timeout);
        }
        let line = line.trim_end_matches('\r');
        match judge(line, deadline) {
            Verdict::Keep => {}
            Verdict::KeepNetwork => out.network_rules += 1,
            Verdict::KeepHide => out.hide_rules += 1,
            Verdict::Drop(reason) => {
                if reason.is_ignored() {
                    out.ignored_exceptions += 1;
                } else {
                    out.dropped += 1;
                }
                out.issue_count += 1;
                if out.issues.len() < MAX_CUSTOM_ISSUES {
                    out.issues.push(RuleIssue {
                        line: index + 1,
                        reason,
                        rule: cut_rule(line),
                    });
                }
                continue;
            }
        }
        if !line.trim().is_empty() {
            out.text.push_str(line);
            out.text.push('\n');
        }
    }
    Ok(out)
}

// `MAX_REPORTED_RULES` is the limit of the downloaded lists' report; the user's
// text has its own, larger one.
const _: () = assert!(MAX_CUSTOM_ISSUES >= MAX_REPORTED_RULES);

#[cfg(test)]
#[path = "custom_tests.rs"]
mod custom_tests;
