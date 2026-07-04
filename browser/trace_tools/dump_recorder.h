/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_DUMP_RECORDER_H_
#define BRAVE_BROWSER_TRACE_TOOLS_DUMP_RECORDER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "brave/browser/trace_tools/dump_writer.h"
#include "chrome/browser/ui/browser_tab_strip_tracker.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

namespace content {
class WebContents;
}  // namespace content

namespace trace_tools {

class DumpTabSession;

// Browser-wide "dump" machinery, mirroring NetworkTraceRecorder. While a domain
// is armed it attaches an in-process DevTools client to every tab on that domain
// and writes each compiled script + loaded HTML document to a timestamped dump
// directory. Lives on the UI thread, owned by BraveTraceService.
class DumpRecorder : public TabStripModelObserver {
 public:
  // Progress for one domain's active/last dump: unique files and total bytes.
  struct Progress {
    int files = 0;
    int64_t bytes = 0;
    bool armed = false;
  };

  DumpRecorder();
  ~DumpRecorder() override;

  DumpRecorder(const DumpRecorder&) = delete;
  DumpRecorder& operator=(const DumpRecorder&) = delete;

  // Arms/disarms dumping for `domain`. Arming opens a fresh dump directory and
  // attaches to open tabs on that domain; disarming detaches (files are already
  // on disk) and keeps the final counts for display.
  void SetDomainArmed(const std::string& domain, bool armed);
  bool IsDomainArmed(const std::string& domain) const;

  // Snapshot of a domain's dump progress (armed counters or the last result).
  Progress GetProgress(const std::string& domain) const;

  // Called by DumpTabSession to persist one captured script/document.
  void AddCapture(const std::string& domain,
                  std::string kind,
                  std::string url,
                  std::string content);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  void AddTab(content::WebContents* contents);
  void RemoveTab(content::WebContents* contents);
  void ReevaluateAllTabs();
  void OnProgress(std::string domain, int files, int64_t bytes);

  std::set<std::string> armed_domains_;
  std::map<std::string, base::SequenceBound<DumpWriter>> writers_;
  std::map<std::string, Progress> progress_;
  std::map<content::WebContents*, std::unique_ptr<DumpTabSession>> sessions_;

  BrowserTabStripTracker tab_strip_tracker_;
  base::WeakPtrFactory<DumpRecorder> weak_factory_{this};
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_DUMP_RECORDER_H_
