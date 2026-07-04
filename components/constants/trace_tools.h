/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_CONSTANTS_TRACE_TOOLS_H_
#define BRAVE_COMPONENTS_CONSTANTS_TRACE_TOOLS_H_

// Local-state pref: TCP port the trace-tools MCP HTTP server binds on
// (127.0.0.1 only). Configurable via brave://settings. Default 7255.
inline constexpr char kTraceToolsMcpPort[] = "brave.trace_tools.mcp_port";

// Local-state pref: master enable for the MCP HTTP server.
inline constexpr char kTraceToolsMcpEnabled[] = "brave.trace_tools.mcp_enabled";

inline constexpr int kTraceToolsDefaultMcpPort = 7255;

#endif  // BRAVE_COMPONENTS_CONSTANTS_TRACE_TOOLS_H_
