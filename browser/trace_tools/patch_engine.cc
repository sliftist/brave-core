/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/patch_engine.h"

#include <string_view>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "brave/browser/trace_tools/trace_domain_util.h"

namespace trace_tools {

namespace {

constexpr char kExampleRuleName[] = "_example";

// Strips a single trailing newline (CRLF or LF) from a match pattern so that a
// prefix/suffix file edited in a text editor still matches minified code.
std::string_view TrimTrailingNewline(std::string_view s) {
  if (!s.empty() && s.back() == '\n') {
    s.remove_suffix(1);
  }
  if (!s.empty() && s.back() == '\r') {
    s.remove_suffix(1);
  }
  return s;
}

// Finds the region of `text` (starting the search at `from`) matched by
// `pattern`, where '*' matches any run of characters. On success returns true
// and sets [*begin, *end) to the span covered by the pattern's literal parts.
// A pattern with no literal characters matches the empty span at `from`.
bool WildcardFind(std::string_view text,
                  std::string_view pattern,
                  size_t from,
                  size_t* begin,
                  size_t* end) {
  size_t pos = from;
  size_t first = std::string_view::npos;
  size_t last_end = from;
  size_t seg_start = 0;
  bool matched_any = false;

  auto match_segment = [&](std::string_view seg) -> bool {
    if (seg.empty()) {
      return true;  // Wildcard-only gap; nothing to anchor.
    }
    size_t idx = text.find(seg, pos);
    if (idx == std::string_view::npos) {
      return false;
    }
    if (!matched_any) {
      first = idx;
      matched_any = true;
    }
    pos = idx + seg.size();
    last_end = pos;
    return true;
  };

  for (size_t i = 0; i <= pattern.size(); ++i) {
    if (i == pattern.size() || pattern[i] == '*') {
      std::string_view seg = pattern.substr(seg_start, i - seg_start);
      if (!match_segment(seg)) {
        return false;
      }
      seg_start = i + 1;
    }
  }

  if (!matched_any) {
    *begin = from;
    *end = from;
    return true;
  }
  *begin = first;
  *end = last_end;
  return true;
}

// Reads the three rule files from `rule_dir`. Returns a rule only if a
// non-empty prefix is present (an empty prefix would match everything).
std::optional<PatchRule> LoadRule(const base::FilePath& rule_dir,
                                  const std::string& name) {
  std::string prefix;
  if (!base::ReadFileToString(rule_dir.AppendASCII("prefix"), &prefix) ||
      prefix.empty()) {
    return std::nullopt;
  }
  std::string suffix;
  base::ReadFileToString(rule_dir.AppendASCII("suffix"), &suffix);
  std::string replace;
  base::ReadFileToString(rule_dir.AppendASCII("replace"), &replace);

  PatchRule rule;
  rule.name = name;
  rule.prefix = std::string(TrimTrailingNewline(prefix));
  rule.suffix = std::string(TrimTrailingNewline(suffix));
  rule.replace = std::move(replace);
  return rule;
}

void SeedExampleRule(const base::FilePath& patches_dir) {
  const base::FilePath example = patches_dir.AppendASCII(kExampleRuleName);
  if (base::DirectoryExists(example)) {
    return;
  }
  if (!base::CreateDirectory(example)) {
    return;
  }
  // Inert sample: the prefix/suffix are unlikely to appear in real code, so
  // this rule documents the format without ever matching anything.
  base::WriteFile(example.AppendASCII("prefix"),
                  "/*__brave_patch_example__ start */");
  base::WriteFile(example.AppendASCII("suffix"),
                  "/* end __brave_patch_example__*/");
  base::WriteFile(example.AppendASCII("replace"),
                  "/* replaced by brave live patch */");
  base::WriteFile(
      example.AppendASCII("README"),
      "Each subfolder here is one live patch rule for this domain.\n"
      "A rule is three files:\n"
      "  prefix  - literal text (may contain '*' wildcards) to find\n"
      "  suffix  - literal text (may contain '*' wildcards) found after prefix\n"
      "  replace - the text that replaces everything from the start of the\n"
      "            prefix match to the end of the suffix match\n"
      "'*' matches any run of characters. Rules are applied to JS and HTML\n"
      "responses served from this domain before they run. This '_example'\n"
      "folder is ignored; copy it to a new name to create a real rule.\n");
}

// Scans a domain's patches folder on the IO sequence. When `scaffold` is set,
// creates the folder and seeds the example rule first.
std::vector<PatchRule> ScanDomainOnIO(std::string domain, bool scaffold) {
  std::vector<PatchRule> rules;
  const base::FilePath patches_dir =
      GetDomainDir(domain).AppendASCII("patches");

  if (scaffold) {
    if (base::CreateDirectory(patches_dir)) {
      SeedExampleRule(patches_dir);
    }
  }
  if (!base::DirectoryExists(patches_dir)) {
    return rules;
  }

  base::FileEnumerator dirs(patches_dir, /*recursive=*/false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = dirs.Next(); !path.empty(); path = dirs.Next()) {
    const std::string name = path.BaseName().AsUTF8Unsafe();
    if (name == kExampleRuleName) {
      continue;
    }
    if (std::optional<PatchRule> rule = LoadRule(path, name)) {
      rules.push_back(std::move(*rule));
    }
  }
  return rules;
}

}  // namespace

PatchRule::PatchRule() = default;
PatchRule::PatchRule(const PatchRule&) = default;
PatchRule& PatchRule::operator=(const PatchRule&) = default;
PatchRule::PatchRule(PatchRule&&) = default;
PatchRule& PatchRule::operator=(PatchRule&&) = default;
PatchRule::~PatchRule() = default;

PatchEngine::DomainStats::DomainStats() = default;
PatchEngine::DomainStats::DomainStats(DomainStats&&) = default;
PatchEngine::DomainStats& PatchEngine::DomainStats::operator=(DomainStats&&) =
    default;
PatchEngine::DomainStats::~DomainStats() = default;

PatchEngine::PatchEngine(scoped_refptr<base::SequencedTaskRunner> io_runner,
                         base::RepeatingClosure on_changed)
    : io_runner_(std::move(io_runner)), on_changed_(std::move(on_changed)) {}

PatchEngine::~PatchEngine() = default;

void PatchEngine::EnsureDomain(const std::string& domain) {
  if (domain.empty() || stats_by_domain_.contains(domain)) {
    return;
  }
  // Insert a placeholder so we only scan once per domain per session.
  stats_by_domain_[domain];
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ScanDomainOnIO, domain, /*scaffold=*/false),
      base::BindOnce(&PatchEngine::OnDomainScanned, weak_factory_.GetWeakPtr(),
                     domain));
}

void PatchEngine::ScaffoldDomain(const std::string& domain) {
  if (domain.empty()) {
    return;
  }
  stats_by_domain_[domain];
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ScanDomainOnIO, domain, /*scaffold=*/true),
      base::BindOnce(&PatchEngine::OnDomainScanned, weak_factory_.GetWeakPtr(),
                     domain));
}

void PatchEngine::OnDomainScanned(const std::string& domain,
                                  std::vector<PatchRule> rules) {
  DomainStats& stats = stats_by_domain_[domain];
  const bool changed = stats.rules.size() != rules.size();
  stats.rules = std::move(rules);
  if (changed && on_changed_) {
    on_changed_.Run();
  }
}

std::optional<std::string> PatchEngine::Apply(const std::string& domain,
                                              const std::string& body) {
  auto it = stats_by_domain_.find(domain);
  if (it == stats_by_domain_.end() || it->second.rules.empty()) {
    return std::nullopt;
  }

  std::string out = body;
  int matched = 0;
  int replaced = 0;
  for (const PatchRule& rule : it->second.rules) {
    size_t pb = 0, pe = 0;
    if (!WildcardFind(out, rule.prefix, 0, &pb, &pe)) {
      continue;
    }
    size_t sb = 0, se = 0;
    if (!WildcardFind(out, rule.suffix, pe, &sb, &se)) {
      continue;
    }
    ++matched;
    out.replace(pb, se - pb, rule.replace);
    ++replaced;
  }

  if (replaced == 0) {
    return std::nullopt;
  }
  it->second.matched += matched;
  it->second.replaced += replaced;
  if (on_changed_) {
    on_changed_.Run();
  }
  return out;
}

int PatchEngine::GetRuleCount(const std::string& domain) const {
  auto it = stats_by_domain_.find(domain);
  return it == stats_by_domain_.end() ? 0 : static_cast<int>(it->second.rules.size());
}

int PatchEngine::GetMatchedCount(const std::string& domain) const {
  auto it = stats_by_domain_.find(domain);
  return it == stats_by_domain_.end() ? 0 : it->second.matched;
}

int PatchEngine::GetReplacedCount(const std::string& domain) const {
  auto it = stats_by_domain_.find(domain);
  return it == stats_by_domain_.end() ? 0 : it->second.replaced;
}

}  // namespace trace_tools
