/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_PATCH_TAB_SESSION_H_
#define BRAVE_BROWSER_TRACE_TOOLS_PATCH_TAB_SESSION_H_

#include <map>
#include <string>

#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class DevToolsAgentHost;
class NavigationHandle;
class WebContents;
}  // namespace content

namespace trace_tools {

class PatchRecorder;

// Wraps a single tab for live patching. While the tab's domain has at least one
// real patch rule it attaches an in-process DevTools client and uses the CDP
// Fetch domain to intercept document/script responses, applying the domain's
// find/replace rules to the response body before it reaches the renderer. The
// browser-side URLLoaderThrottle path only sees main-frame navigations, so Fetch
// interception is what lets us rewrite external script subresources too. One is
// created per tracked WebContents by PatchRecorder.
class PatchTabSession : public content::WebContentsObserver,
                        public content::DevToolsAgentHostClient {
 public:
  PatchTabSession(PatchRecorder* recorder, content::WebContents* web_contents);
  ~PatchTabSession() override;

  PatchTabSession(const PatchTabSession&) = delete;
  PatchTabSession& operator=(const PatchTabSession&) = delete;

  // Attaches or detaches to match whether the current domain has patch rules.
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
  // A response paused by Fetch, awaiting its body so we can decide whether to
  // rewrite (fulfillRequest) or pass it through (continueResponse).
  struct PendingResponse {
    PendingResponse();
    PendingResponse(PendingResponse&&);
    PendingResponse& operator=(PendingResponse&&);
    ~PendingResponse();

    std::string request_id;
    std::string request_domain;
    int response_code = 200;
    std::string response_phrase;
    base::ListValue response_headers;
  };

  void Attach();
  void Detach();
  void SendCommand(const std::string& method, base::DictValue params);
  // Actually hands a built command to the agent host. Always invoked from a
  // fresh task (never synchronously inside DispatchProtocolMessage) so we don't
  // re-enter the DevTools backend while it is delivering a reply, which corrupts
  // its in-flight callback bookkeeping.
  void DispatchToAgent(std::string json);
  void HandleEvent(const std::string& method, const base::DictValue& params);
  void HandleCommandResult(int id, const base::DictValue* result);
  void UpdateCurrentDomain();

  const raw_ptr<PatchRecorder> recorder_;

  std::string current_domain_;
  std::string attached_domain_;

  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  int next_command_id_ = 1;

  // Command id -> paused response awaiting its Fetch.getResponseBody result.
  std::map<int, PendingResponse> body_command_;

  base::WeakPtrFactory<PatchTabSession> weak_factory_{this};
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_PATCH_TAB_SESSION_H_
