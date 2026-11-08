// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/renderer/aerie_cosmetic_agent.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "components/aerie_adblock/common/cosmetic_limits.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/web/web_css_origin.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "v8/include/v8.h"

namespace aerie_adblock {

namespace {

constexpr char kNotifyFunctionName[] = "__aerieCosmeticNotify";

// The text of |value| if it is a string no longer than |max_bytes|. The length
// is checked before the conversion, so a huge string costs nothing.
bool ReadBoundedString(v8::Isolate* isolate,
                       v8::Local<v8::Value> value,
                       size_t max_bytes,
                       std::string* out) {
  if (!value->IsString()) {
    return false;
  }
  v8::Local<v8::String> str = value.As<v8::String>();
  if (static_cast<size_t>(str->Length()) > max_bytes) {
    return false;
  }
  v8::String::Utf8Value utf8(isolate, str);
  if (!*utf8 || static_cast<size_t>(utf8.length()) > max_bytes) {
    return false;
  }
  out->assign(*utf8, utf8.length());
  return true;
}

}  // namespace

// static
void AerieCosmeticAgent::Create(content::RenderFrame* render_frame) {
  new AerieCosmeticAgent(render_frame);
}

AerieCosmeticAgent::AerieCosmeticAgent(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame),
      content::RenderFrameObserverTracker<AerieCosmeticAgent>(render_frame) {
  render_frame->GetAssociatedInterfaceRegistry()
      ->AddInterface<mojom::CosmeticAgent>(base::BindRepeating(
          &AerieCosmeticAgent::BindReceiver, base::Unretained(this)));
}

AerieCosmeticAgent::~AerieCosmeticAgent() = default;

void AerieCosmeticAgent::OnDestruct() {
  delete this;
}

void AerieCosmeticAgent::BindReceiver(
    mojo::PendingAssociatedReceiver<mojom::CosmeticAgent> receiver) {
  receiver_.reset();
  receiver_.Bind(std::move(receiver));
}

void AerieCosmeticAgent::DidCreateNewDocument() {
  // The sheets belonged to the old document and went with it.
  sheets_.Reset();
  gate_.Reset();
  page_key_.reset();
  generic_key_.reset();
  pacer_.Reset();
  logged_insert_cap_ = false;
  generic_timer_.Stop();
}

void AerieCosmeticAgent::SetRules(mojom::CosmeticSheetKind kind,
                                  const std::string& css,
                                  bool append) {
  std::optional<std::string> sheet = sheets_.Apply(kind, css, append);
  if (!sheet) {
    return;  // Refused, or nothing new in it.
  }
  if (kind == mojom::CosmeticSheetKind::kGeneric && append) {
    // Late rules come often; the sheet is inserted at a bounded rate.
    ScheduleGenericInsert();
    return;
  }
  InsertSheet(kind);
}

void AerieCosmeticAgent::ScheduleGenericInsert() {
  GenericInsertPacer::Decision decision =
      pacer_.OnChange(base::TimeTicks::Now());
  switch (decision.kind) {
    case GenericInsertPacer::Decision::kNow:
      InsertGenericNow();
      break;
    case GenericInsertPacer::Decision::kLater:
      if (!generic_timer_.IsRunning()) {
        // The timer inserts whatever the state holds by then.
        generic_timer_.Start(FROM_HERE, decision.delay, this,
                             &AerieCosmeticAgent::InsertGenericNow);
      }
      break;
    case GenericInsertPacer::Decision::kDrop:
      // The document used up its insertions. Say so once.
      if (!logged_insert_cap_) {
        logged_insert_cap_ = true;
        VLOG(1) << "Aerie cosmetic: generic sheet insertion cap reached ("
                << kMaxGenericInsertsPerDocument << "); later rules are not "
                << "applied to this document";
      }
      break;
  }
}

void AerieCosmeticAgent::InsertGenericNow() {
  generic_timer_.Stop();
  pacer_.OnInserted(base::TimeTicks::Now());
  InsertSheet(mojom::CosmeticSheetKind::kGeneric);
}

void AerieCosmeticAgent::InsertSheet(mojom::CosmeticSheetKind kind) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) {
    return;
  }
  blink::WebDocument document = frame->GetDocument();
  std::optional<blink::WebStyleSheetKey>& key =
      kind == mojom::CosmeticSheetKind::kPage ? page_key_ : generic_key_;
  // One sheet of each kind: the new text replaces the old sheet.
  if (key) {
    document.RemoveInsertedStyleSheet(*key, blink::WebCssOrigin::kUser);
    key.reset();
  }
  const std::string& css = sheets_.Text(kind);
  if (css.empty()) {
    return;  // Cleared: nothing to insert.
  }
  key = document.InsertStyleSheet(blink::WebString::FromUtf8(css), nullptr,
                                  blink::WebCssOrigin::kUser);
}

void AerieCosmeticAgent::DidCreateScriptContext(v8::Local<v8::Context> context,
                                                int32_t world_id) {
  // Only the Aerie world gets the function. The page's own scripts run in the
  // main world and cannot see this one.
  if (world_id != aerie_adblock::kCosmeticWorldId) {
    return;
  }
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> function;
  if (!v8::Function::New(context, &AerieCosmeticAgent::OnNotify)
           .ToLocal(&function)) {
    return;
  }
  context->Global()
      ->Set(context,
            v8::String::NewFromUtf8(isolate, kNotifyFunctionName)
                .ToLocalChecked(),
            function)
      .Check();
}

// static
void AerieCosmeticAgent::OnNotify(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  if (info.Length() != 2) {
    return;
  }
  v8::Isolate* isolate = info.GetIsolate();
  blink::WebLocalFrame* frame =
      blink::WebLocalFrame::FrameForContext(isolate->GetCurrentContext());
  content::RenderFrame* render_frame =
      frame ? content::RenderFrame::FromWebFrame(frame) : nullptr;
  AerieCosmeticAgent* agent =
      render_frame ? AerieCosmeticAgent::Get(render_frame) : nullptr;
  std::string classes;
  std::string ids;
  if (!agent ||
      !ReadBoundedString(isolate, info[0], kMaxLateTokenListBytes, &classes) ||
      !ReadBoundedString(isolate, info[1], kMaxLateTokenListBytes, &ids)) {
    return;
  }
  info.GetReturnValue().Set(
      agent->ReportLateTokens(std::move(classes), std::move(ids)));
}

bool AerieCosmeticAgent::ReportLateTokens(std::string classes,
                                          std::string ids) {
  if (!gate_.Allow(classes, ids, base::TimeTicks::Now())) {
    return false;
  }
  if (!host_.is_bound()) {
    render_frame()->GetRemoteAssociatedInterfaces()->GetInterface(&host_);
  }
  host_->LateTokens(classes, ids);
  return true;
}

}  // namespace aerie_adblock
