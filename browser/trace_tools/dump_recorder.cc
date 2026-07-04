/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/dump_recorder.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/task/bind_post_task.h"
#include "base/time/time.h"
#include "brave/browser/trace_tools/brave_trace_service.h"
#include "brave/browser/trace_tools/dump_tab_session.h"
#include "brave/browser/trace_tools/trace_domain_util.h"
#include "content/public/browser/web_contents.h"

namespace trace_tools {

namespace {

std::string TimestampPrefix() {
  base::Time::Exploded exploded;
  base::Time::Now().UTCExplode(&exploded);
  return base::StringPrintf("%04d%02d%02d-%02d%02d%02d", exploded.year,
                            exploded.month, exploded.day_of_month,
                            exploded.hour, exploded.minute, exploded.second);
}

}  // namespace

DumpRecorder::DumpRecorder() : tab_strip_tracker_(this, nullptr) {
  tab_strip_tracker_.Init();
}

DumpRecorder::~DumpRecorder() = default;

void DumpRecorder::SetDomainArmed(const std::string& domain, bool armed) {
  if (domain.empty()) {
    return;
  }
  const bool currently = armed_domains_.contains(domain);
  if (armed == currently) {
    return;
  }

  if (armed) {
    armed_domains_.insert(domain);
    progress_[domain] = Progress{.armed = true};

    const base::FilePath dump_dir = GetDomainDir(domain)
                                        .AppendASCII("dumps")
                                        .AppendASCII(TimestampPrefix());
    auto progress_cb = base::BindPostTaskToCurrentDefault(base::BindRepeating(
        &DumpRecorder::OnProgress, weak_factory_.GetWeakPtr(), domain));
    writers_.insert_or_assign(
        domain, base::SequenceBound<DumpWriter>(
                    BraveTraceService::GetInstance()->io_task_runner(),
                    dump_dir, std::move(progress_cb)));
  } else {
    armed_domains_.erase(domain);
    writers_.erase(domain);  // Destroying the writer flushes; files are on disk.
    auto it = progress_.find(domain);
    if (it != progress_.end()) {
      it->second.armed = false;
    }
  }

  ReevaluateAllTabs();
}

bool DumpRecorder::IsDomainArmed(const std::string& domain) const {
  return armed_domains_.contains(domain);
}

DumpRecorder::Progress DumpRecorder::GetProgress(
    const std::string& domain) const {
  auto it = progress_.find(domain);
  return it != progress_.end() ? it->second : Progress{};
}

void DumpRecorder::AddCapture(const std::string& domain,
                              std::string kind,
                              std::string url,
                              std::string content) {
  auto it = writers_.find(domain);
  if (it == writers_.end()) {
    return;
  }
  it->second.AsyncCall(&DumpWriter::AddFile)
      .WithArgs(std::move(kind), std::move(url), std::move(content));
}

void DumpRecorder::OnProgress(std::string domain, int files, int64_t bytes) {
  auto it = progress_.find(domain);
  if (it == progress_.end()) {
    return;
  }
  it->second.files = files;
  it->second.bytes = bytes;
  BraveTraceService::GetInstance()->NotifyDumpStateChanged();
}

void DumpRecorder::ReevaluateAllTabs() {
  for (auto& [contents, session] : sessions_) {
    session->Evaluate();
  }
}

void DumpRecorder::AddTab(content::WebContents* contents) {
  if (!contents || sessions_.contains(contents)) {
    return;
  }
  auto session = std::make_unique<DumpTabSession>(this, contents);
  session->Evaluate();
  sessions_.insert_or_assign(contents, std::move(session));
}

void DumpRecorder::RemoveTab(content::WebContents* contents) {
  sessions_.erase(contents);
}

void DumpRecorder::OnTabStripModelChanged(
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
