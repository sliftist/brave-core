/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/toolbar/mcp_copy_bubble.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/font_list.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace {

std::string BuildCommand(uint16_t port) {
  return base::StringPrintf(
      "claude mcp add --transport http browser-traces "
      "http://127.0.0.1:%u/mcp",
      static_cast<unsigned>(port));
}

}  // namespace

// static
void MCPCopyBubble::Show(views::View* anchor,
                         uint16_t port,
                         int session_count) {
  views::Widget* widget = views::BubbleDialogDelegateView::CreateBubble(
      std::make_unique<MCPCopyBubble>(anchor, port, session_count));
  widget->Show();
}

MCPCopyBubble::MCPCopyBubble(views::View* anchor,
                             uint16_t port,
                             int session_count)
    : BubbleDialogDelegateView(anchor, views::BubbleBorder::TOP_LEFT),
      command_(BuildCommand(port)) {
  SetShowCloseButton(true);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_fixed_width(420);
  set_margins(gfx::Insets::TLBR(16, 16, 16, 16));

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      /*between_child_spacing=*/12));

  auto* header = AddChildView(std::make_unique<views::Label>(
      u"Browser Traces MCP server"));
  header->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  gfx::FontList font;
  header->SetFontList(
      font.DeriveWithSizeDelta(2).DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));

  std::u16string status =
      port == 0
          ? u"Server is not running."
          : base::ASCIIToUTF16(base::StringPrintf(
                "Listening on 127.0.0.1:%u - %d active session%s",
                static_cast<unsigned>(port), session_count,
                session_count == 1 ? "" : "s"));
  auto* status_label = AddChildView(std::make_unique<views::Label>(status));
  status_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  auto* command_label = AddChildView(
      std::make_unique<views::Label>(base::ASCIIToUTF16(command_)));
  command_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  command_label->SetMultiLine(true);
  command_label->SetSelectable(true);
  command_label->SetFontList(font.DeriveWithStyle(gfx::Font::NORMAL));

  auto* copy = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MCPCopyBubble::OnCopyPressed,
                          base::Unretained(this)),
      u"Copy command"));
  copy->SetStyle(ui::ButtonStyle::kProminent);
  copy->SetHorizontalAlignment(gfx::ALIGN_LEFT);
}

MCPCopyBubble::~MCPCopyBubble() = default;

void MCPCopyBubble::OnCopyPressed() {
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(base::ASCIIToUTF16(command_));
}

BEGIN_METADATA(MCPCopyBubble)
END_METADATA
