/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_BRAVE_TRACE_SERVICE_H_
#define BRAVE_BROWSER_TRACE_TOOLS_BRAVE_TRACE_SERVICE_H_

#include <memory>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/task/sequenced_task_runner.h"

namespace trace_tools {

class NetworkTraceRecorder;

// Browser-process-global owner of all trace-tools state (network tracing, the
// MCP HTTP server, JS/HTML dumping and live patches). Lives for the life of the
// process; access via GetInstance() on the UI thread.
class BraveTraceService {
 public:
  // Observers are notified on the UI thread whenever user-visible state changes
  // (a domain's trace toggles, dump counters update, MCP connection count
  // changes, ...). Toolbar views implement this to stay in sync.
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnTraceStateChanged() {}
  };

  static BraveTraceService* GetInstance();

  BraveTraceService(const BraveTraceService&) = delete;
  BraveTraceService& operator=(const BraveTraceService&) = delete;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Toggles network tracing for `domain` (eTLD+1) and notifies observers.
  void ToggleDomainTrace(const std::string& domain);
  bool IsDomainTraced(const std::string& domain) const;

  // Shared blocking sequence for all trace-tools disk IO.
  scoped_refptr<base::SequencedTaskRunner> io_task_runner() {
    return io_task_runner_;
  }

 protected:
  BraveTraceService();
  ~BraveTraceService();

  void NotifyStateChanged();

 private:
  friend class base::NoDestructor<BraveTraceService>;

  NetworkTraceRecorder* GetRecorder();

  scoped_refptr<base::SequencedTaskRunner> io_task_runner_;
  std::unique_ptr<NetworkTraceRecorder> recorder_;
  base::ObserverList<Observer> observers_;
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_BRAVE_TRACE_SERVICE_H_
