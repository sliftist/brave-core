/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_PATCH_ENGINE_H_
#define BRAVE_BROWSER_TRACE_TOOLS_PATCH_ENGINE_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"

namespace trace_tools {

// One find/replace rule loaded from a domain's patches/<name>/ folder.
// `prefix` and `suffix` are literal match strings that may contain '*'
// wildcards (each '*' matches any run of characters, including none). A rule
// matches when `prefix` is found in the source and `suffix` is found after it;
// the whole span from the start of the prefix match to the end of the suffix
// match is replaced with `replace` verbatim.
struct PatchRule {
  PatchRule();
  PatchRule(const PatchRule&);
  PatchRule& operator=(const PatchRule&);
  PatchRule(PatchRule&&);
  PatchRule& operator=(PatchRule&&);
  ~PatchRule();

  std::string name;
  std::string prefix;
  std::string suffix;
  std::string replace;
};

// Browser-side store of per-domain live patch rules. Owned by
// BraveTraceService and used on the UI thread. Disk scanning happens on the
// shared trace-tools IO sequence; results are cached on the UI thread so that
// the network body handler can apply patches synchronously.
class PatchEngine {
 public:
  PatchEngine(scoped_refptr<base::SequencedTaskRunner> io_runner,
              base::RepeatingClosure on_changed);
  PatchEngine(const PatchEngine&) = delete;
  PatchEngine& operator=(const PatchEngine&) = delete;
  ~PatchEngine();

  // Scans `~/browser-traces/<domain>/patches/` (if it exists) and refreshes the
  // cached rule set for `domain`. Never creates directories. No-op if a scan
  // for this domain is already cached this session (call ForceRefresh to
  // re-scan).
  void EnsureDomain(const std::string& domain);

  // Creates `~/browser-traces/<domain>/patches/` and seeds an inert `_example`
  // rule if the folder does not yet exist, then refreshes the cache. Called
  // when the user explicitly acts on a domain (arms dump / toggles trace).
  void ScaffoldDomain(const std::string& domain);

  // Applies all cached rules for `domain` to `body`. Returns the rewritten body
  // if any rule matched, std::nullopt otherwise. Updates match/replace stats
  // and notifies via the on_changed callback when counts change.
  std::optional<std::string> Apply(const std::string& domain,
                                   const std::string& body);

  // Number of real (non-example) rules cached for `domain`.
  int GetRuleCount(const std::string& domain) const;
  // Cumulative match / replace counts for `domain` this session.
  int GetMatchedCount(const std::string& domain) const;
  int GetReplacedCount(const std::string& domain) const;

 private:
  struct DomainStats {
    DomainStats();
    DomainStats(DomainStats&&);
    DomainStats& operator=(DomainStats&&);
    ~DomainStats();

    std::vector<PatchRule> rules;
    int matched = 0;
    int replaced = 0;
  };

  void OnDomainScanned(const std::string& domain, std::vector<PatchRule> rules);

  scoped_refptr<base::SequencedTaskRunner> io_runner_;
  base::RepeatingClosure on_changed_;
  std::map<std::string, DomainStats> stats_by_domain_;

  base::WeakPtrFactory<PatchEngine> weak_factory_{this};
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_PATCH_ENGINE_H_
