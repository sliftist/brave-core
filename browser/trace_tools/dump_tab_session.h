/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_DUMP_TAB_SESSION_H_
#define BRAVE_BROWSER_TRACE_TOOLS_DUMP_TAB_SESSION_H_

#include <map>
#include <string>

#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class DevToolsAgentHost;
class NavigationHandle;
class WebContents;
}  // namespace content

namespace trace_tools {

class DumpRecorder;

// Wraps a single tab for dump capture. While the tab's domain is armed it
// attaches an in-process DevTools client and records every script the engine
// compiles (Debugger domain) plus each HTML document it loads (Network domain).
// One is created per tracked WebContents by DumpRecorder.
class DumpTabSession : public content::WebContentsObserver,
                       public content::DevToolsAgentHostClient {
 public:
  DumpTabSession(DumpRecorder* recorder, content::WebContents* web_contents);
  ~DumpTabSession() override;

  DumpTabSession(const DumpTabSession&) = delete;
  DumpTabSession& operator=(const DumpTabSession&) = delete;

  // Attaches or detaches to match whether the current domain is armed.
  void Evaluate();

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  std::string GetTypeForMetrics() override;

 private:
  void Attach();
  void Detach();
  void SendCommand(const std::string& method, base::DictValue params);
  void HandleEvent(const std::string& method, const base::DictValue& params);
  void HandleCommandResult(int id, const base::DictValue* result);
  void UpdateCurrentDomain();

  const raw_ptr<DumpRecorder> recorder_;

  std::string current_domain_;
  std::string dump_domain_;

  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  int next_command_id_ = 1;

  // Command id -> source url for in-flight Debugger.getScriptSource calls.
  std::map<int, std::string> script_command_to_url_;
  // Command id -> source url for in-flight Network.getResponseBody (HTML) calls.
  std::map<int, std::string> html_command_to_url_;
  // requestId -> document url for html responses awaiting loadingFinished.
  std::map<std::string, std::string> pending_html_;
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_DUMP_TAB_SESSION_H_
