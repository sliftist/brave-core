/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_MCP_SERVER_H_
#define BRAVE_BROWSER_TRACE_TOOLS_MCP_SERVER_H_

#include <cstdint>
#include <memory>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"

namespace base {
class Thread;
}

namespace trace_tools {

// Serves the on-disk trace store over an MCP streamable-HTTP endpoint
// (JSON-RPC 2.0 at POST /mcp) on 127.0.0.1. The net::HttpServer runs on a
// dedicated IO thread; results are read directly from ~/browser-traces.
//
// Two callbacks report state back to the owner on `ui_task_runner`:
//  * port_cb once the socket is bound (the actual port may differ from the
//    requested one if it was taken), and
//  * session_count_cb whenever the number of MCP sessions seen recently
//    changes (drives the toolbar connection indicator).
class MCPServer {
 public:
  MCPServer(uint16_t desired_port,
            scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
            base::RepeatingCallback<void(uint16_t)> port_cb,
            base::RepeatingCallback<void(int)> session_count_cb);
  ~MCPServer();

  MCPServer(const MCPServer&) = delete;
  MCPServer& operator=(const MCPServer&) = delete;

 private:
  class Core;

  std::unique_ptr<base::Thread> thread_;
  base::SequenceBound<Core> core_;
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_MCP_SERVER_H_
