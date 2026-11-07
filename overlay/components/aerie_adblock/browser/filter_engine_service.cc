// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_engine_service.h"

#include <algorithm>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "components/aerie_adblock/browser/filter_list_state.h"

namespace aerie_adblock {

namespace {
// A list file is never bigger than a download (5 MiB); 8 MiB leaves room.
constexpr size_t kMaxListFileBytes = 8 * 1024 * 1024;
// The time all lists of one rebuild share for checking. EasyList and
// EasyPrivacy together take about 0.2 s.
constexpr base::TimeDelta kBuildBudget = base::Seconds(30);
}  // namespace

// Lives on the service's sequence.
class FilterEngineService::Host {
 public:
  Host() = default;

  struct LoadResult {
    bool ok = false;
    base::TimeDelta elapsed;
  };

  LoadResult Load(std::string_view data) {
    base::ElapsedTimer timer;
    LoadResult result;
    result.ok = engine_.LoadSerialized(base::as_byte_span(data));
    result.elapsed = timer.Elapsed();
    return result;
  }

  bool ShouldBlock(const ClassifiedRequest& request) {
    return engine_.ShouldBlock(request);
  }

  std::string CosmeticStylesheet(const std::string& page_url) {
    return engine_.CosmeticStylesheet(page_url);
  }

  std::string GenericStylesheet(const std::string& page_url,
                                const std::string& classes,
                                const std::string& ids) {
    return engine_.GenericStylesheet(page_url, classes, ids);
}

  BuildResult Replace(std::vector<ListInput> inputs) {
    std::vector<std::string> texts;
    texts.reserve(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
      ListInput& input = inputs[i];
      if (input.text.empty() && !input.path.empty() &&
          !base::ReadFileToStringWithMaxSize(input.path, &input.text,
                                             kMaxListFileBytes)) {
        return BuildResult{ListFailure::kUnreadable, i};
      }
      texts.push_back(std::move(input.text));
    }
    return engine_.LoadLists(texts, kBuildBudget);
  }

 private:
  AdblockEngine engine_;
};

namespace {
FilterEngineService* g_process_instance = nullptr;

// Records how many rules the engine gave for one stylesheet, then passes it
// on. The Bench reads the histogram to tell the engine's rules from any other
// CSS on the page.
void ReportStylesheet(FilterEngineService::StylesheetCallback callback,
                      std::string stylesheet) {
  base::UmaHistogramCounts10000(
      "Aerie.Adblock.CosmeticRuleCount",
      static_cast<int>(std::ranges::count(stylesheet, '\n')));
  std::move(callback).Run(std::move(stylesheet));
}
}  // namespace

// static
FilterEngineService* FilterEngineService::Get() {
  return g_process_instance;
}

// static
void FilterEngineService::CrashOnLoadFailure() {
  CHECK(false) << "aerie_adblock: the filter engine shipped in this build "
                  "could not be loaded. The build is broken.";
}

FilterEngineService::FilterEngineService(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    base::OnceClosure on_load_failure)
    : host_(std::move(task_runner)),
      on_load_failure_(std::move(on_load_failure)) {}

FilterEngineService::~FilterEngineService() {
  if (g_process_instance == this) {
    g_process_instance = nullptr;
  }
}

void FilterEngineService::SetAsProcessInstance() {
  CHECK(!g_process_instance || g_process_instance == this);
  g_process_instance = this;
}


void FilterEngineService::Start(std::string_view data) {
  if (started_) {
    return;
  }
  started_ = true;
  host_.AsyncCall(&Host::Load)
      .WithArgs(data)
      .Then(base::BindOnce(
          [](base::WeakPtr<FilterEngineService> self, Host::LoadResult result) {
            if (self) {
              self->OnLoaded(result.ok, result.elapsed);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void FilterEngineService::ReplaceLists(std::vector<ListInput> lists,
                                       ReplaceCallback callback) {
  host_.AsyncCall(&Host::Replace)
      .WithArgs(std::move(lists))
      .Then(std::move(callback));
}

void FilterEngineService::SetExceptions(std::set<std::string> hosts) {
  exceptions_ = std::move(hosts);
  ReleaseHeldRequests();
}

void FilterEngineService::ReleaseHeldRequests() {
  holding_ = false;
  auto held = std::move(held_);
  held_.clear();
  for (auto& [request, callback] : held) {
    ShouldBlock(std::move(request), std::move(callback));
  }
}

void FilterEngineService::SetBlockedRequestCallback(BlockedCallback callback) {
  blocked_callback_ = std::move(callback);
}

void FilterEngineService::RecordBlocked(
    content::GlobalRenderFrameHostId frame_id) {
  if (blocked_callback_) {
    blocked_callback_.Run(frame_id);
  }
}

void FilterEngineService::ShouldBlock(ClassifiedRequest request,
                                      VerdictCallback callback) {
  if (holding_) {
    held_.emplace_back(std::move(request), std::move(callback));
    return;
  }
  if (!started_ || HostHasException(request.source_hostname, exceptions_)) {
    std::move(callback).Run(false);
    return;
  }
  host_.AsyncCall(&Host::ShouldBlock)
      .WithArgs(std::move(request))
      .Then(std::move(callback));
}

void FilterEngineService::GetCosmeticStylesheet(const GURL& top_level_url,
                                                const GURL& page_url,
                                                StylesheetCallback callback) {
  if (!started_ || !page_url.SchemeIsHTTPOrHTTPS() ||
      !IsCosmeticFilteringEnabled(top_level_url)) {
    std::move(callback).Run(std::string());
    return;
  }
  host_.AsyncCall(&Host::CosmeticStylesheet)
      .WithArgs(page_url.spec())
      .Then(base::BindOnce(&ReportStylesheet, std::move(callback)));
}

void FilterEngineService::GetGenericStylesheet(const GURL& top_level_url,
                                               const GURL& page_url,
                                               std::string classes,
                                               std::string ids,
                                               StylesheetCallback callback) {
  if (!started_ || !page_url.SchemeIsHTTPOrHTTPS() ||
      !IsCosmeticFilteringEnabled(top_level_url)) {
    std::move(callback).Run(std::string());
    return;
  }
  host_.AsyncCall(&Host::GenericStylesheet)
      .WithArgs(page_url.spec(), std::move(classes), std::move(ids))
      .Then(base::BindOnce(&ReportStylesheet, std::move(callback)));
}

void FilterEngineService::SetCosmeticFilteringPolicy(
    CosmeticPolicyCallback policy) {
  cosmetic_policy_ = std::move(policy);
}

bool FilterEngineService::IsCosmeticFilteringEnabled(
    const GURL& top_level_url) const {
  return cosmetic_policy_.is_null() || cosmetic_policy_.Run(top_level_url);
}

void FilterEngineService::OnLoaded(bool ok, base::TimeDelta elapsed) {
  base::UmaHistogramBoolean("Aerie.Adblock.EngineLoadSuccess", ok);
  if (!ok) {
    LOG(ERROR) << "aerie_adblock: the filter engine could not be loaded";
    std::move(on_load_failure_).Run();
    return;
  }
  base::UmaHistogramTimes("Aerie.Adblock.EngineLoadTime", elapsed);
}

}  // namespace aerie_adblock
