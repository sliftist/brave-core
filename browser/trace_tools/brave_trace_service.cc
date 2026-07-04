/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/brave_trace_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "brave/browser/trace_tools/dump_recorder.h"
#include "brave/browser/trace_tools/mcp_server.h"
#include "brave/browser/trace_tools/network_trace_recorder.h"
#include "brave/browser/trace_tools/patch_engine.h"
#include "brave/browser/trace_tools/patch_recorder.h"
#include "brave/components/constants/trace_tools.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_service.h"

namespace trace_tools {

// static
BraveTraceService* BraveTraceService::GetInstance() {
  static base::NoDestructor<BraveTraceService> instance;
  return instance.get();
}

BraveTraceService::BraveTraceService()
    : io_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

BraveTraceService::~BraveTraceService() = default;

void BraveTraceService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
  // The first toolbar view to observe us marks a live browser UI on the UI
  // thread; bring up the live-patch DevTools sessions now so patches for
  // domains that already have rules on disk apply from the next navigation on.
  GetPatchRecorder();
}

void BraveTraceService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

NetworkTraceRecorder* BraveTraceService::GetRecorder() {
  if (!recorder_) {
    recorder_ = std::make_unique<NetworkTraceRecorder>();
  }
  return recorder_.get();
}

void BraveTraceService::ToggleDomainTrace(const std::string& domain) {
  if (domain.empty()) {
    return;
  }
  NetworkTraceRecorder* recorder = GetRecorder();
  recorder->SetDomainTraced(domain, !recorder->IsDomainTraced(domain));
  // Ensure the domain's patches folder exists so the user has somewhere to add
  // rules once they start working on it.
  GetPatchEngine()->ScaffoldDomain(domain);
  NotifyStateChanged();
}

bool BraveTraceService::IsDomainTraced(const std::string& domain) const {
  return recorder_ && recorder_->IsDomainTraced(domain);
}

DumpRecorder* BraveTraceService::GetDumpRecorder() {
  if (!dump_recorder_) {
    dump_recorder_ = std::make_unique<DumpRecorder>();
  }
  return dump_recorder_.get();
}

void BraveTraceService::ToggleDomainDump(const std::string& domain) {
  if (domain.empty()) {
    return;
  }
  DumpRecorder* recorder = GetDumpRecorder();
  recorder->SetDomainArmed(domain, !recorder->IsDomainArmed(domain));
  GetPatchEngine()->ScaffoldDomain(domain);
  NotifyDumpStateChanged();
}

bool BraveTraceService::IsDomainDumpArmed(const std::string& domain) const {
  return dump_recorder_ && dump_recorder_->IsDomainArmed(domain);
}

int BraveTraceService::GetDumpFileCount(const std::string& domain) const {
  return dump_recorder_ ? dump_recorder_->GetProgress(domain).files : 0;
}

int64_t BraveTraceService::GetDumpByteCount(const std::string& domain) const {
  return dump_recorder_ ? dump_recorder_->GetProgress(domain).bytes : 0;
}

void BraveTraceService::NotifyDumpStateChanged() {
  for (Observer& observer : observers_) {
    observer.OnDumpStateChanged();
  }
}

PatchEngine* BraveTraceService::GetPatchEngine() {
  if (!patch_engine_) {
    patch_engine_ = std::make_unique<PatchEngine>(
        io_task_runner_,
        base::BindRepeating(&BraveTraceService::NotifyPatchStateChanged,
                            weak_factory_.GetWeakPtr()));
  }
  return patch_engine_.get();
}

PatchRecorder* BraveTraceService::GetPatchRecorder() {
  if (!patch_recorder_) {
    patch_recorder_ = std::make_unique<PatchRecorder>();
  }
  return patch_recorder_.get();
}

void BraveTraceService::EnsurePatchesForDomain(const std::string& domain) {
  GetPatchEngine()->EnsureDomain(domain);
}

int BraveTraceService::GetPatchCount(const std::string& domain) const {
  return patch_engine_ ? patch_engine_->GetRuleCount(domain) : 0;
}

int BraveTraceService::GetPatchMatchedCount(const std::string& domain) const {
  return patch_engine_ ? patch_engine_->GetMatchedCount(domain) : 0;
}

int BraveTraceService::GetPatchReplacedCount(const std::string& domain) const {
  return patch_engine_ ? patch_engine_->GetReplacedCount(domain) : 0;
}

std::optional<std::string> BraveTraceService::ApplyPatches(
    const std::string& domain,
    const std::string& body) {
  return GetPatchEngine()->Apply(domain, body);
}

void BraveTraceService::ReevaluatePatchTabs() {
  if (patch_recorder_) {
    patch_recorder_->ReevaluateAllTabs();
  }
}

void BraveTraceService::NotifyPatchStateChanged() {
  // A rule set may have just finished loading (async disk scan); let each tab
  // re-check whether it should now be intercepting for its domain. Posted, not
  // called inline: this fires from PatchEngine::Apply while a CDP Fetch reply is
  // being handled, and detaching a session there would free it mid-callback.
  if (patch_recorder_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&BraveTraceService::ReevaluatePatchTabs,
                                  weak_factory_.GetWeakPtr()));
  }
  for (Observer& observer : observers_) {
    observer.OnPatchStateChanged();
  }
}

void BraveTraceService::EnsureMcpServer() {
  if (mcp_server_) {
    return;
  }
  int port = kTraceToolsDefaultMcpPort;
  if (PrefService* local_state = g_browser_process->local_state()) {
    if (!local_state->GetBoolean(kTraceToolsMcpEnabled)) {
      return;
    }
    port = local_state->GetInteger(kTraceToolsMcpPort);
  }
  mcp_server_ = std::make_unique<MCPServer>(
      static_cast<uint16_t>(port),
      base::SequencedTaskRunner::GetCurrentDefault(),
      base::BindRepeating(&BraveTraceService::OnMcpPortBound,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&BraveTraceService::OnMcpSessionCountChanged,
                          weak_factory_.GetWeakPtr()));
}

void BraveTraceService::OnMcpPortBound(uint16_t port) {
  mcp_port_ = port;
  NotifyMcpStateChanged();
}

void BraveTraceService::OnMcpSessionCountChanged(int count) {
  mcp_session_count_ = count;
  NotifyMcpStateChanged();
}

void BraveTraceService::NotifyStateChanged() {
  for (Observer& observer : observers_) {
    observer.OnTraceStateChanged();
  }
}

void BraveTraceService::NotifyMcpStateChanged() {
  for (Observer& observer : observers_) {
    observer.OnMcpStateChanged();
  }
}

}  // namespace trace_tools
