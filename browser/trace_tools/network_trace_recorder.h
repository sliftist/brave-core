/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_NETWORK_TRACE_RECORDER_H_
#define BRAVE_BROWSER_TRACE_TOOLS_NETWORK_TRACE_RECORDER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/values.h"
#include "brave/browser/trace_tools/trace_writer.h"
#include "chrome/browser/ui/browser_tab_strip_tracker.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

namespace content {
class WebContents;
}  // namespace content

namespace trace_tools {

class TabTraceSession;

// Owns the browser-wide network recording machinery. Tracks every tab across
// all browser windows; for each tab whose top-level domain is currently being
// traced it attaches an in-process DevTools client and streams the tab's
// network activity (requests, response bodies, WebSocket frames) into that
// domain's .trace file. Lives on the UI thread, owned by BraveTraceService.
class NetworkTraceRecorder : public TabStripModelObserver {
 public:
  NetworkTraceRecorder();
  ~NetworkTraceRecorder() override;

  NetworkTraceRecorder(const NetworkTraceRecorder&) = delete;
  NetworkTraceRecorder& operator=(const NetworkTraceRecorder&) = delete;

  // Turns tracing for `domain` on/off. Enabling opens a fresh .trace file and
  // attaches to any already-open tabs on that domain; disabling detaches and
  // finalizes the file.
  void SetDomainTraced(const std::string& domain, bool traced);
  bool IsDomainTraced(const std::string& domain) const;

  // Called by TabTraceSession to persist one record for `domain`.
  void WriteRecord(const std::string& domain,
                   uint8_t type,
                   base::DictValue meta,
                   std::vector<uint8_t> body);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  void AddTab(content::WebContents* contents);
  void RemoveTab(content::WebContents* contents);
  // Re-evaluates every tracked tab's attach state (after the traced set change).
  void ReevaluateAllTabs();

  std::set<std::string> traced_domains_;
  std::map<std::string, base::SequenceBound<TraceWriter>> writers_;
  std::map<content::WebContents*, std::unique_ptr<TabTraceSession>> sessions_;

  BrowserTabStripTracker tab_strip_tracker_;
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_NETWORK_TRACE_RECORDER_H_
