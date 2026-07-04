/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_MCP_INDICATOR_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_MCP_INDICATOR_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "brave/browser/trace_tools/brave_trace_service.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "ui/base/metadata/metadata_header_macros.h"

class Browser;

// Toolbar indicator sitting after the Trace button. Starts the MCP HTTP server
// and shows how many MCP clients are currently connected; clicking it opens a
// bubble with the `claude mcp add` command to copy.
class MCPIndicatorView : public ToolbarButton,
                         public trace_tools::BraveTraceService::Observer {
  METADATA_HEADER(MCPIndicatorView, ToolbarButton)

 public:
  explicit MCPIndicatorView(Browser* browser);
  MCPIndicatorView(const MCPIndicatorView&) = delete;
  MCPIndicatorView& operator=(const MCPIndicatorView&) = delete;
  ~MCPIndicatorView() override;

  // trace_tools::BraveTraceService::Observer:
  void OnMcpStateChanged() override;

 private:
  void OnPressed();
  void UpdateState();

  const raw_ptr<Browser> browser_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_MCP_INDICATOR_VIEW_H_
