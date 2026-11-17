// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * The data side of the rename rules editor (issue #146): what a rule looks
 * like in the profile pref aerie.tabs.rename_rules, how the page checks a rule
 * before it saves it, and how a list is reordered. It has no import, so a
 * Python test runs it with node. The browser checks every rule again when it
 * reads the pref (components/aerie_tab_model/rename_rules.cc): this file only
 * saves the user a rule that would be refused.
 */

export type AerieRuleType =
    'domain'|'url-starts-with'|'url-contains'|'title-contains';
export type AerieRewriteMode = 'template'|'remove';

export interface AerieRenameRule {
  id: string;
  name: string;
  type: AerieRuleType;
  value: string;
  mode: AerieRewriteMode;
  text: string;
  enabled: boolean;
}

/** The limits of components/aerie_tab_model/rename_rules.h. */
export const MAX_RULES = 50;
export const MAX_NAME_BYTES = 100;
export const MAX_TEXT_BYTES = 512;

export const RULE_TYPES: AerieRuleType[] =
    ['domain', 'url-starts-with', 'url-contains', 'title-contains'];
export const REWRITE_MODES: AerieRewriteMode[] = ['template', 'remove'];

/** The errors a rule can have; the page owns the words. */
export type AerieRuleError =
    ''|'too-many'|'name-long'|'value-empty'|'value-long'|'text-long'|
    'template-empty'|'remove-empty';

function byteLength(text: string): number {
  return new TextEncoder().encode(text).length;
}

/**
 * A rule from a pref value, which is untrusted: a field of the wrong type
 * makes the whole entry null.
 */
export function ruleFromPref(entry: unknown): AerieRenameRule|null {
  if (typeof entry !== 'object' || entry === null) {
    return null;
  }
  const e = entry as Record<string, unknown>;
  if (typeof e['id'] !== 'string' || typeof e['name'] !== 'string' ||
      typeof e['type'] !== 'string' || typeof e['value'] !== 'string' ||
      typeof e['mode'] !== 'string' || typeof e['text'] !== 'string') {
    return null;
  }
  if (!RULE_TYPES.includes(e['type'] as AerieRuleType) ||
      !REWRITE_MODES.includes(e['mode'] as AerieRewriteMode)) {
    return null;
  }
  return {
    id: e['id'],
    name: e['name'],
    type: e['type'] as AerieRuleType,
    value: e['value'],
    mode: e['mode'] as AerieRewriteMode,
    text: e['text'],
    enabled: e['enabled'] !== false,
  };
}

/** The rules of a pref list, in order, without the entries that are bad. */
export function rulesFromPref(value: unknown): AerieRenameRule[] {
  if (!Array.isArray(value)) {
    return [];
  }
  const rules: AerieRenameRule[] = [];
  for (const entry of value) {
    const rule = ruleFromPref(entry);
    if (rule) {
      rules.push(rule);
    }
  }
  return rules;
}

/** '' when the rule can be saved. */
export function validateRule(
    rule: AerieRenameRule, existingCount: number,
    isNew: boolean): AerieRuleError {
  if (isNew && existingCount >= MAX_RULES) {
    return 'too-many';
  }
  if (byteLength(rule.name) > MAX_NAME_BYTES) {
    return 'name-long';
  }
  if (rule.value.trim() === '') {
    return 'value-empty';
  }
  if (byteLength(rule.value) > MAX_TEXT_BYTES) {
    return 'value-long';
  }
  if (byteLength(rule.text) > MAX_TEXT_BYTES) {
    return 'text-long';
  }
  if (rule.mode === 'template' && rule.text.trim() === '') {
    return 'template-empty';
  }
  if (rule.mode === 'remove' && rule.text.trim() === '' &&
      rule.type !== 'title-contains') {
    return 'remove-empty';
  }
  return '';
}

/** A rule id: 1 to 64 characters of A-Z a-z 0-9 - _ . */
export function newRuleId(now: number, random: number): string {
  return 'r' + Math.floor(now).toString(36) +
      Math.floor(random * 1e9).toString(36);
}

/**
 * `rules` with the rule at `index` moved by `delta` places (-1 up, 1 down), or
 * null when it cannot move. Returns a new list.
 */
export function moveRule(
    rules: AerieRenameRule[], index: number,
    delta: number): AerieRenameRule[]|null {
  return moveRuleTo(rules, index, index + delta);
}

/** Moves the rule at `from` so it ends at `to`; null when nothing moves. */
export function moveRuleTo(
    rules: AerieRenameRule[], from: number,
    to: number): AerieRenameRule[]|null {
  if (!Number.isInteger(from) || !Number.isInteger(to) || from === to ||
      from < 0 || from >= rules.length || to < 0 || to >= rules.length) {
    return null;
  }
  const copy = rules.slice();
  const [moved] = copy.splice(from, 1);
  copy.splice(to, 0, moved);
  return copy;
}

/** A host for the "tidy is off on" list: no scheme, path, port or `www.`. */
export function hostFromInput(text: string): string {
  let host = text.trim().toLowerCase();
  host = host.replace(/^[a-z][a-z0-9+.-]*:\/\//, '');
  host = host.split(/[/?#]/)[0] || '';
  host = host.replace(/:\d+$/, '');
  host = host.replace(/^\*?\./, '').replace(/^www\./, '').replace(/\.$/, '');
  return host;
}
