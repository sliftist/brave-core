/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/toolbar/mcp_indicator_view.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/ui/views/toolbar/mcp_copy_bubble.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace {
constexpr SkColor kConnectedColor = SkColorSetRGB(0x1A, 0x73, 0xE8);
}  // namespace

MCPIndicatorView::MCPIndicatorView(Browser* browser)
    : ToolbarButton(base::BindRepeating(&MCPIndicatorView::OnPressed,
                                        base::Unretained(this))),
      browser_(browser) {
  SetVectorIcon(vector_icons::kLinkIcon);
  auto* service = trace_tools::BraveTraceService::GetInstance();
  service->AddObserver(this);
  service->EnsureMcpServer();
  UpdateState();
}

MCPIndicatorView::~MCPIndicatorView() {
  trace_tools::BraveTraceService::GetInstance()->RemoveObserver(this);
}

void MCPIndicatorView::OnPressed() {
  auto* service = trace_tools::BraveTraceService::GetInstance();
  MCPCopyBubble::Show(this, service->GetMcpPort(),
                      service->GetMcpSessionCount());
}

void MCPIndicatorView::UpdateState() {
  auto* service = trace_tools::BraveTraceService::GetInstance();
  const uint16_t port = service->GetMcpPort();
  const int count = service->GetMcpSessionCount();

  std::u16string text = u"MCP";
  if (count > 0) {
    text = base::StrCat({u"MCP ", base::NumberToString16(count)});
  }
  SetText(text);
  SetEnabledTextColors(count > 0 ? std::optional<SkColor>(kConnectedColor)
                                 : std::nullopt);

  if (port == 0) {
    SetTooltipText(u"Browser Traces MCP server is not running");
  } else {
    SetTooltipText(base::StrCat(
        {u"Browser Traces MCP server on port ",
         base::NumberToString16(port), u" — ",
         base::NumberToString16(count), u" active session(s). Click to copy "
                                        u"the claude mcp add command."}));
  }
}

void MCPIndicatorView::OnMcpStateChanged() {
  UpdateState();
}

BEGIN_METADATA(MCPIndicatorView)
END_METADATA
