/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_MCP_COPY_BUBBLE_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_MCP_COPY_BUBBLE_H_

#include <cstdint>
#include <string>

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

namespace views {
class View;
}

// Bubble anchored to the MCP toolbar indicator. Shows the `claude mcp add`
// command wired to the running server's port with a Copy button.
class MCPCopyBubble : public views::BubbleDialogDelegateView {
  METADATA_HEADER(MCPCopyBubble, views::BubbleDialogDelegateView)

 public:
  MCPCopyBubble(views::View* anchor, uint16_t port, int session_count);
  MCPCopyBubble(const MCPCopyBubble&) = delete;
  MCPCopyBubble& operator=(const MCPCopyBubble&) = delete;
  ~MCPCopyBubble() override;

  static void Show(views::View* anchor, uint16_t port, int session_count);

 private:
  void OnCopyPressed();

  const std::string command_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_MCP_COPY_BUBBLE_H_
