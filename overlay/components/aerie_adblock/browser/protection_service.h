// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_SERVICE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_SERVICE_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"

namespace aerie_adblock {

// The protection category of the Filter Engine: answers "is this host a
// listed phishing or malware domain?". Built like FilterEngineService (read
// its comment; the same decisions apply):
//   - One engine for the browser process, on its own sequence. It is built
//     there and every query runs there.
//   - Queries are held until the engine is ready: every query is posted to the
//     sequence that loads, so it runs after the load. None is answered
//     "not listed" because the engine is still loading.
//   - If the shipped engine cannot be loaded the browser crashes with a CHECK.
//     The engine ships inside the build and the build self-checks it, so a
//     failure means a broken build, and failing open is not allowed. A list
//     with fewer entries than the floor (ProtectionEngine::MinEntries()) is
//     such a failure: it is logged with the count and reported as false in
//     Aerie.Protection.ListLoadSuccess, which the Bench reads.
//   - Owned by the browser's ChromeBrowserMainExtraParts, not a singleton.
//     SetAsProcessInstance() lets the navigation throttle find it; Get() is
//     null in tests and outside that window.
//
// Test list: a build can be asked to add one more list of domains with the
// command line switch --aerie-test-protection-list=<file> (the Bench uses it
// to list a fixture domain). It can only ADD domains to what is blocked, so it
// cannot weaken the protection. The file is read on the engine's sequence.
//
// UI thread only (the public methods), except for the sequence it owns.
class ProtectionService {
 public:
  using ListedCallback = base::OnceCallback<void(bool listed)>;

  static ProtectionService* Get();

  // |on_load_failure| runs on the UI thread if the engine cannot be loaded.
  // The default is a CHECK failure. Tests pass their own.
  // |min_entries| is the fewest domains the shipped list may hold; a list
  // below it is a load failure (an empty or truncated list must fail loudly,
  // not protect nothing). Tests with tiny lists pass a smaller floor.
  explicit ProtectionService(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      base::OnceClosure on_load_failure = base::BindOnce(&CrashOnLoadFailure),
      size_t min_entries = DefaultMinEntries());
  ~ProtectionService();

  ProtectionService(const ProtectionService&) = delete;
  ProtectionService& operator=(const ProtectionService&) = delete;

  // Makes Get() return this service until it is destroyed. At most one.
  void SetAsProcessInstance();

  base::WeakPtr<ProtectionService> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Starts loading the serialized engine in |data|, and the extra list at
  // |extra_list| if it is not empty. Does nothing after the first call. |data|
  // must outlive the process (the resource bundle's data does) or, in tests,
  // the task runner's work. An unreadable |extra_list| is a load failure.
  void Start(std::string_view data, const base::FilePath& extra_list = {});

  bool started() const { return started_; }

  // Answers whether |host| (as GURL::host() gives it) is listed. |callback|
  // runs on the UI thread, after the engine has loaded. If Start() was never
  // called there is no engine and the answer is "not listed".
  void IsListed(std::string host, ListedCallback callback);

 private:
  class Host;

  static void CrashOnLoadFailure();
  static size_t DefaultMinEntries();

  void OnLoaded(bool ok);

  const size_t min_entries_;
  base::SequenceBound<Host> host_;
  base::OnceClosure on_load_failure_;
  bool started_ = false;
  base::WeakPtrFactory<ProtectionService> weak_factory_{this};
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_SERVICE_H_
