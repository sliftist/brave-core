/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/brave_trace_service.h"

#include "base/task/thread_pool.h"

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
}

void BraveTraceService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void BraveTraceService::NotifyStateChanged() {
  for (Observer& observer : observers_) {
    observer.OnTraceStateChanged();
  }
}

}  // namespace trace_tools
