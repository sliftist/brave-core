/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_BRAVE_TRACE_SERVICE_H_
#define BRAVE_BROWSER_TRACE_TOOLS_BRAVE_TRACE_SERVICE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/task/sequenced_task_runner.h"

namespace trace_tools {

class MCPServer;
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
    // Fired when the MCP server binds its port or its active session count
    // changes; drives the toolbar connection indicator.
    virtual void OnMcpStateChanged() {}
  };

  static BraveTraceService* GetInstance();

  BraveTraceService(const BraveTraceService&) = delete;
  BraveTraceService& operator=(const BraveTraceService&) = delete;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Toggles network tracing for `domain` (eTLD+1) and notifies observers.
  void ToggleDomainTrace(const std::string& domain);
  bool IsDomainTraced(const std::string& domain) const;

  // Starts the MCP HTTP server if not already running. Safe to call repeatedly.
  void EnsureMcpServer();
  // Actual bound port (0 until the socket is bound / if the server is off).
  uint16_t GetMcpPort() const { return mcp_port_; }
  // Number of MCP sessions seen recently (drives the toolbar indicator).
  int GetMcpSessionCount() const { return mcp_session_count_; }

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

  void OnMcpPortBound(uint16_t port);
  void OnMcpSessionCountChanged(int count);
  void NotifyMcpStateChanged();

  scoped_refptr<base::SequencedTaskRunner> io_task_runner_;
  std::unique_ptr<NetworkTraceRecorder> recorder_;
  std::unique_ptr<MCPServer> mcp_server_;
  uint16_t mcp_port_ = 0;
  int mcp_session_count_ = 0;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<BraveTraceService> weak_factory_{this};
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_BRAVE_TRACE_SERVICE_H_
