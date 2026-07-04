/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_TRACE_TOOLBAR_BUTTON_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_TRACE_TOOLBAR_BUTTON_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "brave/browser/trace_tools/brave_trace_service.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"

class Browser;

// Toolbar button that replaces the bookmark star. Shows a permanent "Trace"
// label and toggles disk recording of all network traffic for the active tab's
// domain. Reflects the recording state of whatever domain the active tab is
// currently on, following tab switches and same-tab navigations.
class TraceToolbarButton : public ToolbarButton,
                           public TabStripModelObserver,
                           public content::WebContentsObserver,
                           public trace_tools::BraveTraceService::Observer {
  METADATA_HEADER(TraceToolbarButton, ToolbarButton)

 public:
  explicit TraceToolbarButton(Browser* browser);
  TraceToolbarButton(const TraceToolbarButton&) = delete;
  TraceToolbarButton& operator=(const TraceToolbarButton&) = delete;
  ~TraceToolbarButton() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // trace_tools::BraveTraceService::Observer:
  void OnTraceStateChanged() override;

 private:
  void OnPressed();
  void UpdateState();
  std::string GetActiveDomain() const;

  const raw_ptr<Browser> browser_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_TRACE_TOOLBAR_BUTTON_H_
