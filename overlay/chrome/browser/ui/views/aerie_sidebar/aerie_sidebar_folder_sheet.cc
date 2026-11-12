// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_sheet.h"

#include <array>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_switch.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace {

using aerie_tab_model::FolderKind;
using aerie_tab_model::RuleType;

// The kinds the sheet offers, in order. A kind that the product adds later (Smart,
// Behaviour) is one more row here; the rest of the sheet reads this table.
struct KindEntry {
  FolderKind kind;
  int label_id;
};
constexpr std::array<KindEntry, 2> kKinds = {{
    {FolderKind::kManual, IDS_AERIE_SIDEBAR_SHEET_KIND_MANUAL},
    {FolderKind::kAuto, IDS_AERIE_SIDEBAR_SHEET_KIND_AUTO},
}};

struct TypeEntry {
  RuleType type;
  int label_id;
};
constexpr std::array<TypeEntry, 4> kTypes = {{
    {RuleType::kDomain, IDS_AERIE_SIDEBAR_RULE_TYPE_DOMAIN},
    {RuleType::kUrlStartsWith, IDS_AERIE_SIDEBAR_RULE_TYPE_URL_STARTS},
    {RuleType::kUrlContains, IDS_AERIE_SIDEBAR_RULE_TYPE_URL_CONTAINS},
    {RuleType::kTitleContains, IDS_AERIE_SIDEBAR_RULE_TYPE_TITLE_CONTAINS},
}};

std::u16string TypeLabel(RuleType type) {
  for (const TypeEntry& entry : kTypes) {
    if (entry.type == type) {
      return l10n_util::GetStringUTF16(entry.label_id);
    }
  }
  return std::u16string();
}

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        SkColor color,
                                        int size = aerie::tokens::kTypeSizeBody) {
  auto label = std::make_unique<views::Label>(
      text, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4);
  label->SetEnabledColor(color);
  label->SetFontList(
      label->font_list().DeriveWithSizeDelta(size - label->font_list().GetFontSize()));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

std::unique_ptr<views::BoxLayoutView> MakeRow() {
  auto row = std::make_unique<views::BoxLayoutView>();
  row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  row->SetBetweenChildSpacing(aerie::tokens::kSpace8);
  row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  return row;
}

std::unique_ptr<views::BoxLayoutView> MakeColumn(int spacing) {
  auto column = std::make_unique<views::BoxLayoutView>();
  column->SetOrientation(views::BoxLayout::Orientation::kVertical);
  column->SetBetweenChildSpacing(spacing);
  column->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  return column;
}

}  // namespace

// static
AerieSidebarFolderSheet* AerieSidebarFolderSheet::Show(Params params) {
  if (!params.model || !params.model->GetFolder(params.folder_id)) {
    return nullptr;
  }
  auto sheet = base::WrapUnique(new AerieSidebarFolderSheet(std::move(params)));
  AerieSidebarFolderSheet* raw = sheet.get();
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(sheet), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  widget->Show();
  raw->PlayOpen();
  raw->name_field_->RequestFocus();
  raw->name_field_->SelectAll(false);
  return raw;
}

AerieSidebarFolderSheet::AerieSidebarFolderSheet(Params params)
    : views::BubbleDialogDelegate(
          params.anchor,
          params.sidebar_on_right ? views::BubbleBorder::RIGHT_TOP
                                  : views::BubbleBorder::LEFT_TOP,
          views::BubbleBorder::DIALOG_SHADOW),
      folder_id_(params.folder_id),
      model_(params.model),
      prefs_(params.prefs),
      palette_(params.palette),
      sidebar_on_right_(params.sidebar_on_right),
      closed_(std::move(params.closed)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetShowCloseButton(false);
  SetAccessibleWindowRole(ax::mojom::Role::kDialog);
  set_fixed_width(aerie::tokens::kSizeSheetWidth);
  set_margins(gfx::Insets(aerie::tokens::kSpace16));
  Build();
  model_observation_.Observe(model_);
  pref_registrar_.Init(prefs_);
  pref_registrar_.Add(
      aerie_tab_model::kFolderRulesEnabled,
      base::BindRepeating(&AerieSidebarFolderSheet::ScheduleRebuild,
                          base::Unretained(this)));
  RegisterWindowClosingCallback(base::BindOnce(
      &AerieSidebarFolderSheet::OnClosing, base::Unretained(this)));
}

AerieSidebarFolderSheet::~AerieSidebarFolderSheet() {
  slide_motion_.Abort();
  fade_motion_.Abort();
}

void AerieSidebarFolderSheet::Build() {
  const aerie_tab_model::FolderRecord* folder = model_->GetFolder(folder_id_);
  const SkColor text = palette_.text;
  const SkColor muted = palette_.text_muted;
  SetAccessibleTitle(l10n_util::GetStringFUTF16(
      IDS_AERIE_SIDEBAR_SHEET_TITLE, base::UTF8ToUTF16(folder->name)));

  auto contents = MakeColumn(aerie::tokens::kSpace12);

  // Header: the folder glyph and the name.
  auto header = MakeRow();
  auto glyph = std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
      vector_icons::kFolderFilledIcon, muted, 16));
  glyph->GetViewAccessibility().SetIsIgnored(true);
  header->AddChildView(std::move(glyph));
  auto name = std::make_unique<views::Textfield>();
  name->set_controller(this);
  name->SetText(base::UTF8ToUTF16(folder->name));
  name->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_NAME_FIELD));
  name_field_ = header->AddChildView(std::move(name));
  header->SetFlexForView(name_field_, 1);
  contents->AddChildView(std::move(header));

  // Kind: a segmented control. Each segment says `radio` to a screen reader.
  auto kind_row = MakeRow();
  kind_row->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_KIND_LABEL), muted));
  auto segments = MakeRow();
  segments->SetBetweenChildSpacing(aerie::tokens::kSpace2);
  segments->GetViewAccessibility().SetRole(ax::mojom::Role::kRadioGroup);
  segments->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_KIND_LABEL));
  for (size_t i = 0; i < kKinds.size(); ++i) {
    auto segment = std::make_unique<views::LabelButton>(
        base::BindRepeating(&AerieSidebarFolderSheet::SetKind,
                            base::Unretained(this), i),
        l10n_util::GetStringUTF16(kKinds[i].label_id));
    segment->GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    segment->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(aerie::tokens::kSpace4, aerie::tokens::kSpace12)));
    segment->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    kind_buttons_.push_back(segments->AddChildView(std::move(segment)));
  }
  kind_row->AddChildView(std::move(segments));
  contents->AddChildView(std::move(kind_row));

  // The rules, exceptions and the folder's own switch: Auto-filing only.
  auto auto_section = MakeColumn(aerie::tokens::kSpace8);
  auto_section->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_RULES_LABEL), muted,
      aerie::tokens::kTypeSizeCaption));
  auto rules = MakeColumn(aerie::tokens::kSpace4);
  rules->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  rules->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_RULES_LABEL));
  rules_list_ = auto_section->AddChildView(std::move(rules));

  auto add_row = MakeRow();
  std::vector<ui::SimpleComboboxModel::Item> type_names;
  for (const TypeEntry& entry : kTypes) {
    type_names.push_back(ui::SimpleComboboxModel::Item(l10n_util::GetStringUTF16(entry.label_id)));
  }
  auto combo = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(std::move(type_names)));
  combo->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_RULE_TYPE));
  type_combobox_ = add_row->AddChildView(std::move(combo));
  auto value = std::make_unique<views::Textfield>();
  value->set_controller(this);
  value->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_RULE_VALUE));
  value_field_ = add_row->AddChildView(std::move(value));
  add_row->SetFlexForView(value_field_, 1);
  auto add = std::make_unique<views::LabelButton>(
      base::BindRepeating(&AerieSidebarFolderSheet::SubmitRule,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_ADD_RULE));
  add_button_ = add_row->AddChildView(std::move(add));
  auto_section->AddChildView(std::move(add_row));
  rules_full_ = auto_section->AddChildView(
      MakeLabel(l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_RULES_FULL),
                muted, aerie::tokens::kTypeSizeCaption));
  rules_full_->SetVisible(false);

  auto_section->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_EXCEPTIONS_LABEL),
      muted, aerie::tokens::kTypeSizeCaption));
  auto exceptions = MakeColumn(aerie::tokens::kSpace4);
  exceptions->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  exceptions->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_EXCEPTIONS_LABEL));
  exceptions_list_ = auto_section->AddChildView(std::move(exceptions));

  auto all_row = MakeRow();
  auto all_label = all_row->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_ALL_SWITCH), text));
  all_row->SetFlexForView(all_label, 1);
  auto all = std::make_unique<AerieSidebarSwitch>(
      palette_,
      base::BindRepeating(
          [](AerieSidebarFolderSheet* sheet, bool on) {
            aerie_tab_model::SetFolderRulesEnabled(*sheet->model_,
                                                   sheet->folder_id_, on);
          },
          base::Unretained(this)));
  all->SetMotionPrefs(prefs_);
  all->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_ALL_SWITCH));
  all_switch_ = all_row->AddChildView(std::move(all));
  auto_section->AddChildView(std::move(all_row));
  auto_section_ = contents->AddChildView(std::move(auto_section));

  // Empty slots for what later issues add: the body of a Smart folder (#140)
  // and the Behaviour section (#141). They are hidden while empty and take no
  // room; the owner of a later kind fills one and shows it.
  auto smart = MakeColumn(aerie::tokens::kSpace8);
  smart->SetVisible(false);
  smart_body_slot_ = contents->AddChildView(std::move(smart));
  auto behaviour = MakeColumn(aerie::tokens::kSpace8);
  behaviour->SetVisible(false);
  behaviour_slot_ = contents->AddChildView(std::move(behaviour));

  // Footer: the global switch and Done.
  auto footer = MakeRow();
  auto global_label = footer->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_GLOBAL_SWITCH), text));
  footer->SetFlexForView(global_label, 1);
  auto global = std::make_unique<AerieSidebarSwitch>(
      palette_, base::BindRepeating(
                    [](AerieSidebarFolderSheet* sheet, bool on) {
                      sheet->prefs_->SetBoolean(
                          aerie_tab_model::kFolderRulesEnabled, on);
                    },
                    base::Unretained(this)));
  global->SetMotionPrefs(prefs_);
  global->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_GLOBAL_SWITCH));
  global_switch_ = footer->AddChildView(std::move(global));
  contents->AddChildView(std::move(footer));
  auto done_row = MakeRow();
  done_row->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kEnd);
  auto done = std::make_unique<views::LabelButton>(
      base::BindRepeating(&AerieSidebarFolderSheet::Dismiss,
                          base::Unretained(this), /*refocus=*/true),
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_DONE));
  done->SetEnabledTextColors(color_utils::GetColorWithMaxContrast(palette_.accent));
  done->SetBackground(views::CreateRoundedRectBackground(
      palette_.accent, aerie::tokens::kRadiusMd));
  done->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(aerie::tokens::kSpace4, aerie::tokens::kSpace16)));
  done_button_ = done_row->AddChildView(std::move(done));
  contents->AddChildView(std::move(done_row));

  contents_ = SetContentsView(std::move(contents));
  contents_->SetPaintToLayer();
  contents_->layer()->SetFillsBoundsOpaquely(false);
  SetInitiallyFocusedView(name_field_);
  Rebuild();
}

views::LabelButton* AerieSidebarFolderSheet::kind_button(size_t index) const {
  return index < kind_buttons_.size() ? kind_buttons_[index].get() : nullptr;
}

bool AerieSidebarFolderSheet::kind_checked(size_t index) const {
  const aerie_tab_model::FolderRecord* folder = model_->GetFolder(folder_id_);
  return folder && index < kKinds.size() &&
         kKinds[index].kind ==
             (aerie_tab_model::IsAutoFiling(*folder) ? FolderKind::kAuto
                                                     : FolderKind::kManual);
}

ui::Layer* AerieSidebarFolderSheet::slide_layer() const {
  return contents_ ? contents_->layer() : nullptr;
}

ui::Layer* AerieSidebarFolderSheet::fade_layer() const {
  return GetWidget() ? const_cast<views::Widget*>(GetWidget())->GetLayer() : nullptr;
}

void AerieSidebarFolderSheet::ScheduleRebuild() {
  if (rebuild_pending_ || closing_) {
    return;
  }
  rebuild_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<AerieSidebarFolderSheet> self) {
                       if (self && self->rebuild_pending_) {
                         self->Rebuild();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void AerieSidebarFolderSheet::FlushForTesting() {
  if (rebuild_pending_) {
    Rebuild();
  }
}

void AerieSidebarFolderSheet::Rebuild() {
  rebuild_pending_ = false;
  const aerie_tab_model::FolderRecord* folder = model_->GetFolder(folder_id_);
  if (!folder) {
    return;
  }
  const bool is_auto = aerie_tab_model::IsAutoFiling(*folder);
  for (size_t i = 0; i < kKinds.size(); ++i) {
    const bool checked = kind_checked(i);
    views::LabelButton* segment = kind_buttons_[i];
    segment->GetViewAccessibility().SetCheckedState(
        checked ? ax::mojom::CheckedState::kTrue
                : ax::mojom::CheckedState::kFalse);
    // The chosen segment is filled with the Workspace accent: the sidebar's
    // translucent row colours would not show on the sheet's own background.
    segment->SetEnabledTextColors(checked ? color_utils::GetColorWithMaxContrast(palette_.accent)
                                          : palette_.text_muted);
    segment->SetBackground(
        checked ? views::CreateRoundedRectBackground(palette_.accent,
                                                     aerie::tokens::kRadiusMd)
                : nullptr);
  }
  auto_section_->SetVisible(is_auto);
  global_switch_->SetOn(
      aerie_tab_model::AreFolderRulesEnabled(*prefs_), prefs_);
  all_switch_->SetOn(aerie_tab_model::ReadFolderRules(*folder).enabled, prefs_);
  if (is_auto) {
    RebuildRules(*folder);
    RebuildExceptions(*folder);
  }
  if (GetWidget()) {
    SizeToContents();
  }
}

void AerieSidebarFolderSheet::RebuildRules(
    const aerie_tab_model::FolderRecord& folder) {
  const aerie_tab_model::FolderRules rules = aerie_tab_model::ReadFolderRules(folder);
  rules_list_->RemoveAllChildViews();
  for (const aerie_tab_model::Rule& rule : rules.rules) {
    const std::u16string text = l10n_util::GetStringFUTF16(
        IDS_AERIE_SIDEBAR_RULE_CHIP, TypeLabel(rule.type),
        base::UTF8ToUTF16(rule.value));
    auto chip = MakeRow();
    chip->GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
    chip->GetViewAccessibility().SetName(text);
    auto toggle = std::make_unique<AerieSidebarSwitch>(
        palette_, base::BindRepeating(
                      [](AerieSidebarFolderSheet* sheet, std::string id,
                         bool on) {
                        aerie_tab_model::SetRuleEnabled(
                            *sheet->model_, sheet->folder_id_, id, on);
                      },
                      base::Unretained(this), rule.id));
    toggle->SetMotionPrefs(prefs_);
    toggle->GetViewAccessibility().SetName(
        l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_RULE_CHIP_OFF, text));
    toggle->SetOn(rule.enabled, nullptr);
    chip->AddChildView(std::move(toggle));
    auto edit = std::make_unique<views::LabelButton>(
        base::BindRepeating(&AerieSidebarFolderSheet::BeginEditRule,
                            base::Unretained(this), rule.id),
        text);
    edit->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    edit->SetEnabledTextColors(palette_.text);
    edit->SetTooltipText(text);
    chip->SetFlexForView(chip->AddChildView(std::move(edit)), 1);
    auto remove = views::CreateVectorImageButton(base::BindRepeating(
        [](AerieSidebarFolderSheet* sheet, std::string id) {
          aerie_tab_model::RemoveRule(*sheet->model_, sheet->folder_id_, id);
        },
        base::Unretained(this), rule.id));
    for (views::Button::ButtonState state :
         {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
          views::Button::STATE_PRESSED, views::Button::STATE_DISABLED}) {
      remove->SetImageModel(
          state, ui::ImageModel::FromVectorIcon(vector_icons::kCloseSmallIcon,
                                                palette_.text_muted, 16));
    }
    remove->GetViewAccessibility().SetName(
        l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_RULE_REMOVE, text));
    remove->SetTooltipText(
        l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_RULE_REMOVE, text));
    chip->AddChildView(std::move(remove));
    rules_list_->AddChildView(std::move(chip));
  }
  const bool full = rules.rules.size() >= aerie_tab_model::kMaxRulesPerFolder;
  rules_full_->SetVisible(full);
  add_button_->SetEnabled(!full || !editing_rule_id_.empty());
  add_button_->SetText(l10n_util::GetStringUTF16(
      editing_rule_id_.empty() ? IDS_AERIE_SIDEBAR_SHEET_ADD_RULE
                               : IDS_AERIE_SIDEBAR_SHEET_SAVE_RULE));
}

void AerieSidebarFolderSheet::RebuildExceptions(
    const aerie_tab_model::FolderRecord& folder) {
  const aerie_tab_model::FolderRules rules = aerie_tab_model::ReadFolderRules(folder);
  exceptions_list_->RemoveAllChildViews();
  for (const std::string& host : rules.excluded_hosts) {
    auto row = MakeRow();
    row->GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
    row->GetViewAccessibility().SetName(base::UTF8ToUTF16(host));
    row->SetFlexForView(
        row->AddChildView(MakeLabel(base::UTF8ToUTF16(host), palette_.text)), 1);
    auto remove = views::CreateVectorImageButton(base::BindRepeating(
        [](AerieSidebarFolderSheet* sheet, std::string host) {
          aerie_tab_model::RemoveExcludedHost(*sheet->model_, sheet->folder_id_,
                                              host);
        },
        base::Unretained(this), host));
    for (views::Button::ButtonState state :
         {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
          views::Button::STATE_PRESSED, views::Button::STATE_DISABLED}) {
      remove->SetImageModel(
          state, ui::ImageModel::FromVectorIcon(vector_icons::kCloseSmallIcon,
                                                palette_.text_muted, 16));
    }
    const std::u16string label = l10n_util::GetStringFUTF16(
        IDS_AERIE_SIDEBAR_SHEET_EXCEPTION_REMOVE, base::UTF8ToUTF16(host));
    remove->GetViewAccessibility().SetName(label);
    remove->SetTooltipText(label);
    row->AddChildView(std::move(remove));
    exceptions_list_->AddChildView(std::move(row));
  }
  if (rules.excluded_hosts.empty()) {
    auto none = MakeLabel(
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SHEET_EXCEPTIONS_EMPTY),
        palette_.text_muted, aerie::tokens::kTypeSizeCaption);
    exceptions_list_->AddChildView(std::move(none));
  }
}

void AerieSidebarFolderSheet::SetKind(size_t index) {
  if (index >= kKinds.size()) {
    return;
  }
  aerie_tab_model::SetFolderRuleKind(*model_, folder_id_, kKinds[index].kind);
}

void AerieSidebarFolderSheet::BeginEditRule(const std::string& rule_id) {
  const aerie_tab_model::FolderRecord* folder = model_->GetFolder(folder_id_);
  if (!folder) {
    return;
  }
  for (const aerie_tab_model::Rule& rule :
       aerie_tab_model::ReadFolderRules(*folder).rules) {
    if (rule.id != rule_id) {
      continue;
    }
    editing_rule_id_ = rule.id;
    for (size_t i = 0; i < kTypes.size(); ++i) {
      if (kTypes[i].type == rule.type) {
        type_combobox_->SetSelectedIndex(i);
      }
    }
    value_field_->SetText(base::UTF8ToUTF16(rule.value));
    value_field_->RequestFocus();
    value_field_->SelectAll(false);
    ScheduleRebuild();
    return;
  }
}

void AerieSidebarFolderSheet::SubmitRule() {
  const std::string value = base::UTF16ToUTF8(value_field_->GetText());
  const auto index = type_combobox_->GetSelectedIndex();
  if (value.empty() || !index || *index >= kTypes.size()) {
    return;
  }
  const RuleType type = kTypes[*index].type;
  bool done;
  if (editing_rule_id_.empty()) {
    done = !aerie_tab_model::AddRule(*model_, folder_id_, type, value).empty();
  } else {
    done = aerie_tab_model::UpdateRule(*model_, folder_id_, editing_rule_id_,
                                       type, value);
    editing_rule_id_.clear();
  }
  if (done) {
    value_field_->SetText(std::u16string());
  }
  ScheduleRebuild();
  value_field_->RequestFocus();
}

void AerieSidebarFolderSheet::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (sender == name_field_) {
    // Live: an empty name keeps the old one (RenameFolder), and the field keeps
    // what the user is typing.
    aerie_tab_model::RenameFolder(*model_, folder_id_,
                                  base::UTF16ToUTF8(new_contents));
  }
}

bool AerieSidebarFolderSheet::HandleKeyEvent(views::Textfield* sender,
                                             const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    Dismiss(/*refocus=*/true);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_RETURN && sender == value_field_) {
    SubmitRule();
    return true;
  }
  if (key_event.key_code() == ui::VKEY_RETURN && sender == name_field_) {
    Dismiss(/*refocus=*/true);
    return true;
  }
  return false;
}

void AerieSidebarFolderSheet::OnFolderChanged(
    const aerie_tab_model::FolderRecord& folder) {
  if (folder.id == folder_id_) {
    ScheduleRebuild();
  }
}

void AerieSidebarFolderSheet::OnFolderRemoved(
    const aerie_tab_model::FolderRecord& folder) {
  if (folder.id == folder_id_) {
    Dismiss(/*refocus=*/false);
  }
}

void AerieSidebarFolderSheet::PlayOpen() {
  const PrefService* prefs = prefs_;
  const aerie_sidebar::SheetMotion motion = aerie_sidebar::SheetEnterMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  ui::Layer* fade = fade_layer();
  ui::Layer* slide = slide_layer();
  if (fade) {
    fade->SetOpacity(motion.from_opacity);
    aerie::motion::LayerMotion target;
    target.opacity = motion.to_opacity;
    fade_motion_ = aerie::motion::AnimateLayer(fade, target, motion.duration,
                                               motion.easing, prefs);
  }
  if (slide && motion.animate_slide) {
    const int from = sidebar_on_right_ ? motion.slide_px : -motion.slide_px;
    slide->SetTransform(gfx::Transform::MakeTranslation(from, 0));
    aerie::motion::LayerMotion target;
    target.transform = gfx::Transform();
    slide_motion_ = aerie::motion::AnimateLayer(slide, target, motion.duration,
                                                motion.easing, prefs);
  }
}

bool AerieSidebarFolderSheet::Cancel() {
  Dismiss(/*refocus=*/true);
  return false;
}

void AerieSidebarFolderSheet::Dismiss(bool refocus) {
  if (closing_) {
    return;
  }
  closing_ = true;
  refocus_ = refocus;
  views::Widget* widget = GetWidget();
  if (!widget || widget->IsClosed()) {
    return;
  }
  const aerie_sidebar::SheetMotion motion = aerie_sidebar::SheetExitMotion(
      aerie::motion::ShouldReduceMotion(prefs_));
  fade_motion_.Abort();
  if (ui::Layer* fade = fade_layer()) {
    aerie::motion::LayerMotion target;
    target.opacity = motion.to_opacity;
    fade_motion_ = aerie::motion::AnimateLayer(fade, target, motion.duration,
                                               motion.easing, prefs_);
  }
  close_timer_.Start(FROM_HERE, base::Milliseconds(motion.duration_ms),
                     base::BindOnce(&AerieSidebarFolderSheet::FinishClose,
                                    base::Unretained(this)));
}

void AerieSidebarFolderSheet::FinishClose() {
  if (views::Widget* widget = GetWidget(); widget && !widget->IsClosed()) {
    widget->Close();
  }
}

void AerieSidebarFolderSheet::OnClosing() {
  closing_ = true;
  close_timer_.Stop();
  if (closed_) {
    std::move(closed_).Run(refocus_);
  }
}
