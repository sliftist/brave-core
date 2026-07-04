/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_DUMP_TOOLBAR_BUTTON_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_DUMP_TOOLBAR_BUTTON_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "brave/browser/trace_tools/brave_trace_service.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"

class Browser;

// Toolbar button between the Trace button and the MCP indicator. Click once to
// arm JS+HTML dumping for the active tab's domain (showing a live file count and
// size); click again to stop. Reflects the state of whatever domain the active
// tab is on, following tab switches and same-tab navigations.
class DumpToolbarButton : public ToolbarButton,
                          public TabStripModelObserver,
                          public content::WebContentsObserver,
                          public trace_tools::BraveTraceService::Observer {
  METADATA_HEADER(DumpToolbarButton, ToolbarButton)

 public:
  explicit DumpToolbarButton(Browser* browser);
  DumpToolbarButton(const DumpToolbarButton&) = delete;
  DumpToolbarButton& operator=(const DumpToolbarButton&) = delete;
  ~DumpToolbarButton() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // trace_tools::BraveTraceService::Observer:
  void OnDumpStateChanged() override;

 private:
  void OnPressed();
  void UpdateState();
  std::string GetActiveDomain() const;

  const raw_ptr<Browser> browser_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_DUMP_TOOLBAR_BUTTON_H_
