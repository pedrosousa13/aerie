// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

//! The bridge for the protection category (phishing and malware domains).
//! Same conventions as the ad engine bridge in `lib.rs`: no `Result`, no
//! panics, ASCII strings, one sequence at a time.

use aerie_adblock_core::safety::ProtectionList;

/// Named apart from the C++ wrapper class `aerie_adblock::ProtectionEngine`, which cxx would clash with.
pub struct ProtectionDomains(ProtectionList);

#[cxx::bridge(namespace = "aerie_adblock")]
mod ffi {
    extern "Rust" {
        type ProtectionDomains;

        /// An engine with no domains. It lists nothing.
        fn new_empty_protection_engine() -> Box<ProtectionDomains>;

        /// Build an engine from one list (domain per line or hosts format).
        /// A list that is too large gives an empty engine. For tests; the
        /// browser loads the serialized form the build made.
        fn new_protection_engine_from_list(list: &[u8]) -> Box<ProtectionDomains>;

        /// Replace the domains with a serialized engine. Returns false, and
        /// keeps the old domains, when `data` is not valid.
        fn load_serialized(self: &mut ProtectionDomains, data: &[u8]) -> bool;

        /// The form `load_serialized` reads. For tests.
        fn serialize(self: &ProtectionDomains) -> Vec<u8>;

        /// Is `host` a listed domain, or a subdomain of one?
        fn is_listed(self: &ProtectionDomains, host: &str) -> bool;

        /// How many domains are listed.
        fn domain_count(self: &ProtectionDomains) -> usize;
    }
}

fn new_empty_protection_engine() -> Box<ProtectionDomains> {
    Box::new(ProtectionDomains(ProtectionList::empty()))
}

fn new_protection_engine_from_list(list: &[u8]) -> Box<ProtectionDomains> {
    let engine = ProtectionList::from_lists([list]).unwrap_or_else(|_| ProtectionList::empty());
    Box::new(ProtectionDomains(engine))
}

impl ProtectionDomains {
    fn load_serialized(&mut self, data: &[u8]) -> bool {
        self.0.load_serialized(data).is_ok()
    }

    fn serialize(&self) -> Vec<u8> {
        self.0.serialize()
    }

    fn is_listed(&self, host: &str) -> bool {
        self.0.is_listed(host)
    }

    fn domain_count(&self) -> usize {
        self.0.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bridge_lists_hosts_and_subdomains() {
        let e = new_protection_engine_from_list(b"evil.example\n0.0.0.0 bad.example\n");
        assert!(e.is_listed("evil.example"));
        assert!(e.is_listed("a.evil.example"));
        assert!(e.is_listed("bad.example"));
        assert!(!e.is_listed("good.example"));
        assert_eq!(e.domain_count(), 2);
    }

    #[test]
    fn bridge_load_serialized_reports_failure_and_keeps_domains() {
        let mut e = new_protection_engine_from_list(b"evil.example\n");
        assert!(!e.load_serialized(b"nope"));
        assert!(e.is_listed("evil.example"));
        let bytes = e.serialize();
        let mut fresh = new_empty_protection_engine();
        assert!(!fresh.is_listed("evil.example"));
        assert!(fresh.load_serialized(&bytes));
        assert!(fresh.is_listed("evil.example"));
    }

    #[test]
    fn bridge_oversize_list_gives_an_empty_engine() {
        let huge = vec![b'a'; aerie_adblock_core::MAX_LIST_BYTES + 1];
        assert_eq!(new_protection_engine_from_list(&huge).domain_count(), 0);
    }
}
