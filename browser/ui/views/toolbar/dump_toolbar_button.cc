/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/toolbar/dump_toolbar_button.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/trace_tools/trace_domain_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace {

constexpr SkColor kArmedColor = SkColorSetRGB(0xEA, 0x43, 0x35);

std::u16string FormatSize(int64_t bytes) {
  if (bytes < 1024) {
    return base::StrCat({base::NumberToString16(bytes), u" B"});
  }
  if (bytes < 1024 * 1024) {
    return base::StrCat({base::NumberToString16(bytes / 1024), u" KB"});
  }
  return base::StrCat(
      {base::NumberToString16(bytes / (1024 * 1024)), u" MB"});
}

}  // namespace

DumpToolbarButton::DumpToolbarButton(Browser* browser)
    : ToolbarButton(base::BindRepeating(&DumpToolbarButton::OnPressed,
                                        base::Unretained(this))),
      browser_(browser) {
  SetVectorIcon(vector_icons::kFileDownloadChromeRefreshIcon);
  browser_->tab_strip_model()->AddObserver(this);
  trace_tools::BraveTraceService::GetInstance()->AddObserver(this);
  UpdateState();
}

DumpToolbarButton::~DumpToolbarButton() {
  trace_tools::BraveTraceService::GetInstance()->RemoveObserver(this);
  if (browser_) {
    browser_->tab_strip_model()->RemoveObserver(this);
  }
}

std::string DumpToolbarButton::GetActiveDomain() const {
  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return std::string();
  }
  return trace_tools::GetTraceDomain(contents->GetLastCommittedURL());
}

void DumpToolbarButton::OnPressed() {
  const std::string domain = GetActiveDomain();
  if (domain.empty()) {
    return;
  }
  trace_tools::BraveTraceService::GetInstance()->ToggleDomainDump(domain);
}

void DumpToolbarButton::UpdateState() {
  // Follow the active tab so same-tab navigations refresh our state.
  Observe(browser_->tab_strip_model()->GetActiveWebContents());

  auto* service = trace_tools::BraveTraceService::GetInstance();
  const std::string domain = GetActiveDomain();
  if (!domain.empty()) {
    // Discover this domain's patch rules so the count below is populated.
    service->EnsurePatchesForDomain(domain);
  }
  const bool armed =
      !domain.empty() && service->IsDomainDumpArmed(domain);
  const int files = domain.empty() ? 0 : service->GetDumpFileCount(domain);
  const int64_t bytes = domain.empty() ? 0 : service->GetDumpByteCount(domain);

  std::u16string label;
  if (armed) {
    label = base::StrCat({u"Dumping ", base::NumberToString16(files), u" (",
                          FormatSize(bytes), u")"});
    SetEnabledTextColors(std::optional<SkColor>(kArmedColor));
    SetTooltipText(u"Stop dumping JS + HTML for this site");
  } else if (files > 0) {
    label = base::StrCat({u"Dumped ", base::NumberToString16(files)});
    SetEnabledTextColors(std::nullopt);
    SetTooltipText(u"Dump JS + HTML for this site to disk");
  } else {
    label = u"Dump";
    SetEnabledTextColors(std::nullopt);
    SetTooltipText(u"Dump all JS + HTML for this site to disk");
  }

  const int patches = domain.empty() ? 0 : service->GetPatchCount(domain);
  if (patches > 0) {
    const int replaced = service->GetPatchReplacedCount(domain);
    label = base::StrCat({label, u" | ", base::NumberToString16(patches),
                          u" patch", patches == 1 ? u"" : u"es"});
    if (replaced > 0) {
      label = base::StrCat({label, u", ", base::NumberToString16(replaced),
                            u" replaced"});
    }
  }
  SetText(label);
}

void DumpToolbarButton::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  UpdateState();
}

void DumpToolbarButton::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted()) {
    UpdateState();
  }
}

void DumpToolbarButton::OnDumpStateChanged() {
  UpdateState();
}

void DumpToolbarButton::OnPatchStateChanged() {
  UpdateState();
}

BEGIN_METADATA(DumpToolbarButton)
END_METADATA
