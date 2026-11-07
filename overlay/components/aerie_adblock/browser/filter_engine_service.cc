// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_engine_service.h"

#include <algorithm>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "components/aerie_adblock/browser/adblock_engine.h"

namespace aerie_adblock {

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

void FilterEngineService::ShouldBlock(ClassifiedRequest request,
                                      VerdictCallback callback) {
  if (!started_) {
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
