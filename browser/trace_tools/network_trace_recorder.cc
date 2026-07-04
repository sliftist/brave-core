/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/network_trace_recorder.h"

#include <utility>

#include "base/i18n/time_formatting.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "brave/browser/trace_tools/brave_trace_service.h"
#include "brave/browser/trace_tools/tab_trace_session.h"
#include "brave/browser/trace_tools/trace_domain_util.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/web_contents.h"

namespace trace_tools {

namespace {

// Filesystem-friendly UTC timestamp prefix so files sort chronologically:
// e.g. "20260704-131502".
std::string TimestampPrefix() {
  base::Time::Exploded exploded;
  base::Time::Now().UTCExplode(&exploded);
  return base::StringPrintf("%04d%02d%02d-%02d%02d%02d", exploded.year,
                            exploded.month, exploded.day_of_month,
                            exploded.hour, exploded.minute, exploded.second);
}

}  // namespace

NetworkTraceRecorder::NetworkTraceRecorder()
    : tab_strip_tracker_(this, nullptr) {
  tab_strip_tracker_.Init();
}

NetworkTraceRecorder::~NetworkTraceRecorder() = default;

void NetworkTraceRecorder::SetDomainTraced(const std::string& domain,
                                           bool traced) {
  if (domain.empty()) {
    return;
  }
  const bool currently = traced_domains_.contains(domain);
  if (traced == currently) {
    return;
  }

  auto io_runner = BraveTraceService::GetInstance()->io_task_runner();
  if (traced) {
    traced_domains_.insert(domain);

    base::DictValue header;
    header.Set("version", 1);
    header.Set("domain", domain);
    header.Set("started_iso",
               base::TimeFormatAsIso8601(base::Time::Now()));
    header.Set("browser_version",
               std::string(version_info::GetVersionNumber()));
    header.Set("platform", std::string(version_info::GetOSType()));

    const base::FilePath path =
        GetDomainDir(domain).AppendASCII(TimestampPrefix() + ".trace");
    writers_.insert_or_assign(
        domain, base::SequenceBound<TraceWriter>(io_runner, path,
                                                 std::move(header)));
  } else {
    traced_domains_.erase(domain);
    auto it = writers_.find(domain);
    if (it != writers_.end()) {
      it->second.AsyncCall(&TraceWriter::Finalize)
          .WithArgs(base::DictValue());
      writers_.erase(it);
    }
  }

  ReevaluateAllTabs();
}

bool NetworkTraceRecorder::IsDomainTraced(const std::string& domain) const {
  return traced_domains_.contains(domain);
}

void NetworkTraceRecorder::WriteRecord(const std::string& domain,
                                       uint8_t type,
                                       base::DictValue meta,
                                       std::vector<uint8_t> body) {
  auto it = writers_.find(domain);
  if (it == writers_.end()) {
    return;
  }
  it->second.AsyncCall(&TraceWriter::AppendRecord)
      .WithArgs(type, std::move(meta), std::move(body));
}

void NetworkTraceRecorder::ReevaluateAllTabs() {
  for (auto& [contents, session] : sessions_) {
    session->Evaluate();
  }
}

void NetworkTraceRecorder::AddTab(content::WebContents* contents) {
  if (!contents || sessions_.contains(contents)) {
    return;
  }
  auto session = std::make_unique<TabTraceSession>(this, contents);
  session->Evaluate();
  sessions_.insert_or_assign(contents, std::move(session));
}

void NetworkTraceRecorder::RemoveTab(content::WebContents* contents) {
  sessions_.erase(contents);
}

void NetworkTraceRecorder::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  switch (change.type()) {
    case TabStripModelChange::kInserted:
      for (const auto& item : change.GetInsert()->contents) {
        AddTab(item.contents);
      }
      break;
    case TabStripModelChange::kRemoved:
      for (const auto& item : change.GetRemove()->contents) {
        RemoveTab(item.contents);
      }
      break;
    case TabStripModelChange::kReplaced: {
      const auto* replace = change.GetReplace();
      RemoveTab(replace->old_contents);
      AddTab(replace->new_contents);
      break;
    }
    case TabStripModelChange::kMoved:
    case TabStripModelChange::kSelectionOnly:
      break;
  }
}

}  // namespace trace_tools
