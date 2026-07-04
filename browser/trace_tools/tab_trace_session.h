/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_TAB_TRACE_SESSION_H_
#define BRAVE_BROWSER_TRACE_TOOLS_TAB_TRACE_SESSION_H_

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

class NetworkTraceRecorder;

// Wraps a single tab. Watches its top-level navigation to know the tab's
// current domain, and — while that domain is being traced — attaches an
// in-process DevTools client that captures the tab's network traffic. One is
// created per tracked WebContents by NetworkTraceRecorder.
class TabTraceSession : public content::WebContentsObserver,
                        public content::DevToolsAgentHostClient {
 public:
  TabTraceSession(NetworkTraceRecorder* recorder,
                  content::WebContents* web_contents);
  ~TabTraceSession() override;

  TabTraceSession(const TabTraceSession&) = delete;
  TabTraceSession& operator=(const TabTraceSession&) = delete;

  // Attaches or detaches to match whether the current domain is traced.
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

  const raw_ptr<NetworkTraceRecorder> recorder_;

  // eTLD+1 of the tab's last committed main-frame URL.
  std::string current_domain_;
  // Domain the currently-attached session is writing into (frozen at attach).
  std::string trace_domain_;

  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  int next_command_id_ = 1;

  // Command id -> requestId for in-flight Network.getResponseBody calls.
  std::map<int, std::string> body_command_to_request_;
  // requestId -> response meta awaiting its body.
  std::map<std::string, base::DictValue> pending_responses_;
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_TAB_TRACE_SESSION_H_
