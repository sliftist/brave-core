/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_PATCH_RECORDER_H_
#define BRAVE_BROWSER_TRACE_TOOLS_PATCH_RECORDER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "chrome/browser/ui/browser_tab_strip_tracker.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

namespace content {
class WebContents;
}  // namespace content

namespace trace_tools {

class PatchTabSession;

// Browser-wide live-patch machinery, mirroring NetworkTraceRecorder. It keeps a
// PatchTabSession per open tab; each session attaches an in-process DevTools
// client and rewrites document/script bodies (via the CDP Fetch domain) for any
// domain that has real patch rules on disk. Lives on the UI thread, owned by
// BraveTraceService. Rule storage/matching lives in PatchEngine (reached through
// BraveTraceService); this class only manages the per-tab DevTools sessions.
class PatchRecorder : public TabStripModelObserver {
 public:
  PatchRecorder();
  ~PatchRecorder() override;

  PatchRecorder(const PatchRecorder&) = delete;
  PatchRecorder& operator=(const PatchRecorder&) = delete;

  // Thin delegations to BraveTraceService, used by PatchTabSession.
  bool HasRulesForDomain(const std::string& domain) const;
  void EnsureRulesForDomain(const std::string& domain);
  std::optional<std::string> ApplyPatches(const std::string& domain,
                                          const std::string& body);

  // Re-checks every tab's attach state. Called when the cached rule set changes
  // (e.g. an async disk scan finished) so interception turns on once rules for a
  // freshly visited domain are known.
  void ReevaluateAllTabs();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  void AddTab(content::WebContents* contents);
  void RemoveTab(content::WebContents* contents);

  std::map<content::WebContents*, std::unique_ptr<PatchTabSession>> sessions_;

  BrowserTabStripTracker tab_strip_tracker_;
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_PATCH_RECORDER_H_
