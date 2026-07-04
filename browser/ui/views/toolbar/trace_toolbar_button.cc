/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/toolbar/trace_toolbar_button.h"

#include <utility>

#include "base/functional/bind.h"
#include "brave/browser/trace_tools/trace_domain_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace {
constexpr SkColor kRecordingColor = SkColorSetRGB(0xEA, 0x43, 0x35);
}  // namespace

TraceToolbarButton::TraceToolbarButton(Browser* browser)
    : ToolbarButton(base::BindRepeating(&TraceToolbarButton::OnPressed,
                                        base::Unretained(this))),
      browser_(browser) {
  SetVectorIcon(vector_icons::kCodeChromeRefreshIcon);
  browser_->tab_strip_model()->AddObserver(this);
  trace_tools::BraveTraceService::GetInstance()->AddObserver(this);
  UpdateState();
}

TraceToolbarButton::~TraceToolbarButton() {
  trace_tools::BraveTraceService::GetInstance()->RemoveObserver(this);
  if (browser_) {
    browser_->tab_strip_model()->RemoveObserver(this);
  }
}

std::string TraceToolbarButton::GetActiveDomain() const {
  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return std::string();
  }
  return trace_tools::GetTraceDomain(contents->GetLastCommittedURL());
}

void TraceToolbarButton::OnPressed() {
  const std::string domain = GetActiveDomain();
  if (domain.empty()) {
    return;
  }
  trace_tools::BraveTraceService::GetInstance()->ToggleDomainTrace(domain);
}

void TraceToolbarButton::UpdateState() {
  // Follow the active tab so same-tab navigations refresh our state.
  Observe(browser_->tab_strip_model()->GetActiveWebContents());

  const std::string domain = GetActiveDomain();
  const bool traced =
      !domain.empty() &&
      trace_tools::BraveTraceService::GetInstance()->IsDomainTraced(domain);

  SetText(traced ? u"Recording" : u"Trace");
  SetEnabledTextColors(traced ? std::optional<SkColor>(kRecordingColor)
                              : std::nullopt);
  SetTooltipText(traced ? u"Stop recording network traffic for this site"
                        : u"Record network traffic for this site to disk");
}

void TraceToolbarButton::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  UpdateState();
}

void TraceToolbarButton::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted()) {
    UpdateState();
  }
}

void TraceToolbarButton::OnTraceStateChanged() {
  UpdateState();
}

BEGIN_METADATA(TraceToolbarButton)
END_METADATA
