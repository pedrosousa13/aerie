// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_ENGINE_SERVICE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_ENGINE_SERVICE_H_

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "components/aerie_adblock/browser/list_failure.h"
#include "components/aerie_adblock/browser/request_classifier.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/global_routing_id.h"
#include "url/gurl.h"

namespace aerie_adblock {

// The Filter Engine service: owns the one adblock engine of the browser
// process and answers "block this request?".
//
// Design points (decided in issue #8):
//   - One engine for the whole browser process, shared by every profile.
//   - The engine lives on its own sequence (a thread pool sequence). It is
//     built there and every query runs there, so the engine needs no locks
//     and the UI thread never parses a list or matches a rule.
//   - Requests are held until the engine is ready. Every query is posted to
//     the same sequence as the load, so it runs after the load, in order.
//     No query is answered "allow" because the engine is still loading.
//   - It runs in the browser process only. Nothing here is reachable from a
//     renderer.
//
// If the shipped engine cannot be loaded, the browser crashes with a CHECK.
// The engine ships inside the build and the build self-checks it
// (aerie_adblock_compile), so a load failure means a broken build, and a
// browser that quietly stopped blocking would be worse than one that stops.
//
// Ownership: not a singleton. The browser's ChromeBrowserMainExtraParts
// (chrome/browser/aerie_adblock) owns it, creates it before any profile exists
// and destroys it at shutdown. SetAsProcessInstance() lets the URLLoaderFactory
// hook find it; Get() is null in tests and outside that window, and holders
// keep a WeakPtr.
//
// UI thread only (the public methods), except for the sequence it owns.
// The key under which the per-site exceptions of an off-the-record profile
// live (FilterEngineService::SetSessionExceptions): the profile itself, as an
// opaque pointer. Null for a regular profile (or none), whose exceptions are
// saved to disk. Never dereferenced.
inline const void* SessionKeyFor(content::BrowserContext* browser_context) {
  return browser_context && browser_context->IsOffTheRecord() ? browser_context
                                                              : nullptr;
}

class FilterEngineService {
 public:
  using VerdictCallback = base::OnceCallback<void(bool should_block)>;
  // |stylesheet| is CSS text, or empty when nothing is to be hidden.
  using StylesheetCallback = base::OnceCallback<void(std::string stylesheet)>;
  // Says whether cosmetic filtering is on for the page whose top-level URL is
  // given. The hook for the per-site switch (issue #10): that code installs
  // its own policy with SetCosmeticFilteringPolicy().
  using CosmeticPolicyCallback = base::RepeatingCallback<bool(const GURL&)>;

  // The instance the browser registered with SetAsProcessInstance(), or null.
  static FilterEngineService* Get();

  // |on_load_failure| runs on the UI thread if the engine cannot be loaded. The
  // default is a CHECK failure. Tests pass their own.
  explicit FilterEngineService(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      base::OnceClosure on_load_failure = base::BindOnce(&CrashOnLoadFailure));
  ~FilterEngineService();

  // Makes Get() return this service until it is destroyed. At most one.
  void SetAsProcessInstance();

  base::WeakPtr<FilterEngineService> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  FilterEngineService(const FilterEngineService&) = delete;
  FilterEngineService& operator=(const FilterEngineService&) = delete;

  // Starts loading the serialized engine in |data|. Does nothing after the
  // first call. |data| must outlive the process (the resource bundle's data
  // does) or, in tests, the task runner's work.
  void Start(std::string_view data);

  bool started() const { return started_; }

  // One filter list for ReplaceLists(): its text, or, when |text| is empty,
  // the file to read it from (read on the engine's sequence).
  struct ListInput {
    std::string text;
    base::FilePath path;
  };
  using ReplaceCallback = base::OnceCallback<void(BuildResult)>;

  // Compiles |lists| together and replaces the engine's rules with them. The
  // work runs on the engine's sequence, after the load and every earlier
  // query. If any list is bad (unreadable, not a list, too slow) nothing
  // changes and |callback| gets the failure and the list's index; the old
  // rules, which keep blocking, stay. Later queries wait for the build.
  void ReplaceLists(std::vector<ListInput> lists, ReplaceCallback callback);

  // Sites where blocking is off, as set by the user (issue #10). A request
  // whose top-level site is, or is a subdomain of, one of these is never
  // blocked. Hosts are lowercase (see NormalizeExceptionHost()).
  void SetExceptions(std::set<std::string> hosts);
  const std::set<std::string>& exceptions() const { return exceptions_; }

  // Sites where blocking is off for one off-the-record session only (issue
  // #10). |session| is an opaque key for the session (the incognito profile);
  // a request or page with the same key is excepted like in SetExceptions(),
  // any other is not. Memory only: nothing here is ever written to disk. An
  // empty |hosts| removes the session's entry; the owner calls it when the
  // session ends.
  void SetSessionExceptions(const void* session, std::set<std::string> hosts);
  // True when |host| is excepted for everyone, or for |session|.
  bool IsExcepted(std::string_view host, const void* session) const;

  // From now until SetExceptions() or ReleaseHeldRequests(), every query waits
  // (it is neither answered nor blocked). The filter list manager reads the
  // user's exceptions from disk without blocking the UI thread; this keeps the
  // first requests of a restored session from being judged before the
  // exceptions are known. The manager always ends the hold, also when it is
  // destroyed.
  void HoldRequestsUntilSettingsLoaded() { holding_ = true; }
  void ReleaseHeldRequests();

  // Called, on the UI thread, for every request the engine blocked, with the
  // frame the request came from (invalid for workers and the like). One
  // callback; the page counter (page_block_counter.h) sets it.
  using BlockedCallback =
      base::RepeatingCallback<void(content::GlobalRenderFrameHostId)>;
  void SetBlockedRequestCallback(BlockedCallback callback);
  // The proxies call this after a "block" verdict.
  void RecordBlocked(content::GlobalRenderFrameHostId frame_id);

  // Answers whether |request| must be blocked. |callback| runs on the UI
  // thread. It runs after the engine has loaded. If Start() was never called
  // there is no engine and the answer is "allow".
  void ShouldBlock(ClassifiedRequest request, VerdictCallback callback);

  // Cosmetic filtering (issue #9). Both answer with CSS that hides elements
  // and run |callback| on the UI thread, after the engine has loaded. The
  // answer is empty when the engine was never started, when |page_url| is
  // not http(s), when the policy says cosmetic filtering is off for
  // |top_level_url|, and when no rule applies. |top_level_url| is the URL of
  // the page's outermost main frame; |page_url| is the frame's own.
  //
  // The stylesheet from GetCosmeticStylesheet() is for when the frame has
  // committed (rules for the site, and generic rules that do not start with a
  // class or id). GetGenericStylesheet() is for when the frame has loaded:
  // |classes| and |ids| are whitespace separated lists of what the page uses.
  //
  // The policy is not the only input: a site with a per-site exception (see
  // SetExceptions(), SetSessionExceptions()) gets no stylesheet either, so
  // "blocking off" also stops hiding. |session| is the off-the-record session
  // key of the page's profile, null for a regular one. While the settings load
  // (HoldRequestsUntilSettingsLoaded) these calls wait, like ShouldBlock().
  void GetCosmeticStylesheet(const GURL& top_level_url,
                             const GURL& page_url,
                             StylesheetCallback callback,
                             const void* session = nullptr);
  void GetGenericStylesheet(const GURL& top_level_url,
                            const GURL& page_url,
                            std::string classes,
                            std::string ids,
                            StylesheetCallback callback,
                            const void* session = nullptr);

  // Installs the policy that GetCosmeticStylesheet() and GetGenericStylesheet()
  // ask. The default (no policy) is: on everywhere. Does not touch
  // stylesheets already injected into open pages.
  void SetCosmeticFilteringPolicy(CosmeticPolicyCallback policy);
  // True when the policy allows |top_level_url| and the site has no exception.
  bool IsCosmeticFilteringEnabled(const GURL& top_level_url,
                                  const void* session = nullptr) const;

 private:
  class Host;

  static void CrashOnLoadFailure();

  void OnLoaded(bool ok, base::TimeDelta elapsed);

  base::SequenceBound<Host> host_;
  std::set<std::string> exceptions_;
  std::map<const void*, std::set<std::string>> session_exceptions_;
  std::vector<base::OnceClosure> held_cosmetic_;
  bool holding_ = false;
  std::vector<std::pair<ClassifiedRequest, VerdictCallback>> held_;
  BlockedCallback blocked_callback_;
  base::OnceClosure on_load_failure_;
  CosmeticPolicyCallback cosmetic_policy_;
  bool started_ = false;
  base::WeakPtrFactory<FilterEngineService> weak_factory_{this};
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_ENGINE_SERVICE_H_
