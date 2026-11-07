// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/protection_service.h"

#include <optional>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "components/aerie_adblock/browser/protection_engine.h"

namespace aerie_adblock {

namespace {

// More than any real test list; keeps a wrong path from filling memory.
constexpr size_t kMaxExtraListBytes = 8 * 1024 * 1024;

ProtectionService* g_process_instance = nullptr;

}  // namespace

// Lives on the service's sequence.
class ProtectionService::Host {
 public:
  Host() = default;

  bool Load(std::string_view data, base::FilePath extra_list) {
    if (!engine_.LoadSerialized(base::as_byte_span(data))) {
      return false;
    }
    if (!extra_list.empty()) {
      std::string text;
      if (!base::ReadFileToStringWithMaxSize(extra_list, &text,
                                             kMaxExtraListBytes)) {
        LOG(ERROR) << "aerie_adblock: cannot read the test protection list";
        return false;
      }
      extra_.emplace();
      extra_->LoadListForTesting(text);
    }
    return true;
  }

  bool IsListed(const std::string& host) {
    return engine_.IsListed(host) || (extra_ && extra_->IsListed(host));
  }

 private:
  ProtectionEngine engine_;
  std::optional<ProtectionEngine> extra_;
};

// static
ProtectionService* ProtectionService::Get() {
  return g_process_instance;
}

// static
void ProtectionService::CrashOnLoadFailure() {
  CHECK(false) << "aerie_adblock: the protection list shipped in this build "
                  "could not be loaded. The build is broken.";
}

ProtectionService::ProtectionService(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    base::OnceClosure on_load_failure)
    : host_(std::move(task_runner)),
      on_load_failure_(std::move(on_load_failure)) {}

ProtectionService::~ProtectionService() {
  if (g_process_instance == this) {
    g_process_instance = nullptr;
  }
}

void ProtectionService::SetAsProcessInstance() {
  CHECK(!g_process_instance || g_process_instance == this);
  g_process_instance = this;
}

void ProtectionService::Start(std::string_view data,
                              const base::FilePath& extra_list) {
  if (started_) {
    return;
  }
  started_ = true;
  host_.AsyncCall(&Host::Load)
      .WithArgs(data, extra_list)
      .Then(base::BindOnce(
          [](base::WeakPtr<ProtectionService> self, bool ok) {
            if (self) {
              self->OnLoaded(ok);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void ProtectionService::IsListed(std::string host, ListedCallback callback) {
  if (!started_) {
    std::move(callback).Run(false);
    return;
  }
  host_.AsyncCall(&Host::IsListed)
      .WithArgs(std::move(host))
      .Then(std::move(callback));
}

void ProtectionService::OnLoaded(bool ok) {
  base::UmaHistogramBoolean("Aerie.Protection.ListLoadSuccess", ok);
  if (!ok) {
    LOG(ERROR) << "aerie_adblock: the protection list could not be loaded";
    std::move(on_load_failure_).Run();
  }
}

}  // namespace aerie_adblock
