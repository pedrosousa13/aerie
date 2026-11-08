// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_RENDERER_AERIE_COSMETIC_AGENT_H_
#define COMPONENTS_AERIE_ADBLOCK_RENDERER_AERIE_COSMETIC_AGENT_H_

#include <optional>
#include <string>

#include "base/timer/timer.h"
#include "components/aerie_adblock/common/cosmetic.mojom.h"
#include "components/aerie_adblock/common/late_report_gate.h"
#include "components/aerie_adblock/renderer/cosmetic_sheet_state.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/web/web_document.h"
#include "v8/include/v8-forward.h"

namespace aerie_adblock {

// The renderer piece of cosmetic filtering (ADR 0005, issue #68). One per
// frame. It does two things and nothing else:
//
//   1. Inserts the hide rules that the browser sends as a user-origin sheet
//      (`WebDocument::InsertStyleSheet(..., kUser)`). The page cannot see,
//      remove or override such a sheet: a user-origin `!important` rule beats
//      an author `!important` one.
//   2. Gives the browser's late-element watch script (a fixed script in the
//      Aerie isolated world) a native function,
//      `__aerieCosmeticNotify(classes, ids)`, which sends the two strings to
//      the browser as lookup keys.
//
// What it does not do: it applies only CSS the browser sends and adds no
// selector; it has no network access and no URL; it reads no filter list and
// does not parse the CSS; its Mojo interface is the two calls in
// cosmetic.mojom. Every input is bounded by cosmetic_limits.h. The browser
// decides everything else, including whether cosmetic filtering is on.
//
// Created for each frame by patches/adblock/0012.
class AerieCosmeticAgent
    : public content::RenderFrameObserver,
      public content::RenderFrameObserverTracker<AerieCosmeticAgent>,
      public mojom::CosmeticAgent {
 public:
  // Makes the agent of |render_frame|; it deletes itself with the frame.
  static void Create(content::RenderFrame* render_frame);

  AerieCosmeticAgent(const AerieCosmeticAgent&) = delete;
  AerieCosmeticAgent& operator=(const AerieCosmeticAgent&) = delete;

  // mojom::CosmeticAgent:
  void SetRules(mojom::CosmeticSheetKind kind,
                const std::string& css,
                bool append) override;

  // content::RenderFrameObserver:
  void OnDestruct() override;
  void DidCreateNewDocument() override;
  void DidCreateScriptContext(v8::Local<v8::Context> context,
                              int32_t world_id) override;

 private:
  explicit AerieCosmeticAgent(content::RenderFrame* render_frame);
  ~AerieCosmeticAgent() override;

  void BindReceiver(
      mojo::PendingAssociatedReceiver<mojom::CosmeticAgent> receiver);

  // The native function the watch script calls. Checks its two arguments.
  // Returns true to the script when the report was accepted and sent, so it
  // can forget the tokens; false keeps them for another try.
  static void OnNotify(const v8::FunctionCallbackInfo<v8::Value>& info);
  bool ReportLateTokens(std::string classes, std::string ids);

  // Replaces the sheet of |kind| in the document with the state's text for it.
  void InsertSheet(mojom::CosmeticSheetKind kind);
  // The generic sheet changed: insert it now, later or never, per the pacer.
  void ScheduleGenericInsert();
  void InsertGenericNow();

  CosmeticSheetState sheets_;
  LateReportGate gate_;
  GenericInsertPacer pacer_;
  bool logged_insert_cap_ = false;
  base::OneShotTimer generic_timer_;
  // Keys of the sheets in the current document, for removing them.
  std::optional<blink::WebStyleSheetKey> page_key_;
  std::optional<blink::WebStyleSheetKey> generic_key_;
  mojo::AssociatedReceiver<mojom::CosmeticAgent> receiver_{this};
  mojo::AssociatedRemote<mojom::CosmeticHost> host_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_RENDERER_AERIE_COSMETIC_AGENT_H_
