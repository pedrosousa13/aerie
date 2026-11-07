// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_ENGINE_SERVICE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_ENGINE_SERVICE_H_

#include <string_view>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"
#include "components/aerie_adblock/browser/request_classifier.h"

namespace aerie_adblock {

class AdblockEngine;

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
class FilterEngineService {
 public:
  using VerdictCallback = base::OnceCallback<void(bool should_block)>;

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

  // Answers whether |request| must be blocked. |callback| runs on the UI
  // thread. It runs after the engine has loaded. If Start() was never called
  // there is no engine and the answer is "allow".
  void ShouldBlock(ClassifiedRequest request, VerdictCallback callback);

 private:
  class Host;

  static void CrashOnLoadFailure();

  void OnLoaded(bool ok, base::TimeDelta elapsed);

  base::SequenceBound<Host> host_;
  base::OnceClosure on_load_failure_;
  bool started_ = false;
  base::WeakPtrFactory<FilterEngineService> weak_factory_{this};
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_ENGINE_SERVICE_H_
