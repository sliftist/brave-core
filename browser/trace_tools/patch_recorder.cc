/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/patch_recorder.h"

#include <utility>

#include "brave/browser/trace_tools/brave_trace_service.h"
#include "brave/browser/trace_tools/patch_tab_session.h"
#include "content/public/browser/web_contents.h"

namespace trace_tools {

PatchRecorder::PatchRecorder() : tab_strip_tracker_(this, nullptr) {
  tab_strip_tracker_.Init();
}

PatchRecorder::~PatchRecorder() = default;

bool PatchRecorder::HasRulesForDomain(const std::string& domain) const {
  return BraveTraceService::GetInstance()->GetPatchCount(domain) > 0;
}

void PatchRecorder::EnsureRulesForDomain(const std::string& domain) {
  BraveTraceService::GetInstance()->EnsurePatchesForDomain(domain);
}

std::optional<std::string> PatchRecorder::ApplyPatches(
    const std::string& domain,
    const std::string& body) {
  return BraveTraceService::GetInstance()->ApplyPatches(domain, body);
}

void PatchRecorder::ReevaluateAllTabs() {
  for (auto& [contents, session] : sessions_) {
    session->Evaluate();
  }
}

void PatchRecorder::AddTab(content::WebContents* contents) {
  if (!contents || sessions_.contains(contents)) {
    return;
  }
  auto session = std::make_unique<PatchTabSession>(this, contents);
  session->Evaluate();
  sessions_.insert_or_assign(contents, std::move(session));
}

void PatchRecorder::RemoveTab(content::WebContents* contents) {
  sessions_.erase(contents);
}

void PatchRecorder::OnTabStripModelChanged(
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
