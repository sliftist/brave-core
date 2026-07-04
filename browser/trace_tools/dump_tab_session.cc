/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/dump_tab_session.h"

#include <string_view>
#include <utility>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "brave/browser/trace_tools/dump_recorder.h"
#include "brave/browser/trace_tools/trace_domain_util.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

namespace trace_tools {

DumpTabSession::DumpTabSession(DumpRecorder* recorder,
                               content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents), recorder_(recorder) {
  UpdateCurrentDomain();
}

DumpTabSession::~DumpTabSession() {
  Detach();
}

void DumpTabSession::UpdateCurrentDomain() {
  if (!web_contents()) {
    current_domain_.clear();
    return;
  }
  current_domain_ = GetTraceDomain(web_contents()->GetLastCommittedURL());
}

void DumpTabSession::Evaluate() {
  const bool should_dump =
      !current_domain_.empty() && recorder_->IsDomainArmed(current_domain_);
  if (should_dump) {
    if (!agent_host_) {
      Attach();
    } else if (dump_domain_ != current_domain_) {
      Detach();
      Attach();
    }
  } else if (agent_host_) {
    Detach();
  }
}

void DumpTabSession::Attach() {
  if (agent_host_ || !web_contents()) {
    return;
  }
  agent_host_ = content::DevToolsAgentHost::GetOrCreateFor(web_contents());
  if (!agent_host_->AttachClient(this)) {
    agent_host_.reset();
    return;
  }
  dump_domain_ = current_domain_;

  // Debugger domain surfaces every compiled script; Network captures the raw
  // HTML documents as they load.
  SendCommand("Debugger.enable", base::DictValue());
  base::DictValue net_params;
  net_params.Set("maxTotalBufferSize", 100 * 1024 * 1024);
  net_params.Set("maxResourceBufferSize", 50 * 1024 * 1024);
  SendCommand("Network.enable", std::move(net_params));
}

void DumpTabSession::Detach() {
  if (!agent_host_) {
    return;
  }
  agent_host_->DetachClient(this);
  agent_host_.reset();
  dump_domain_.clear();
  script_command_to_url_.clear();
  html_command_to_url_.clear();
  pending_html_.clear();
}

void DumpTabSession::SendCommand(const std::string& method,
                                 base::DictValue params) {
  if (!agent_host_) {
    return;
  }
  base::DictValue command;
  command.Set("id", next_command_id_++);
  command.Set("method", method);
  command.Set("params", std::move(params));

  std::string json;
  base::JSONWriter::Write(command, &json);
  agent_host_->DispatchProtocolMessage(this, base::as_byte_span(json));
}

void DumpTabSession::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted()) {
    return;
  }
  UpdateCurrentDomain();
  Evaluate();
}

void DumpTabSession::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  std::optional<base::Value> value = base::JSONReader::Read(
      std::string_view(reinterpret_cast<const char*>(message.data()),
                       message.size()),
      base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return;
  }
  const base::DictValue& dict = value->GetDict();

  if (const std::string* method = dict.FindString("method")) {
    const base::DictValue* params = dict.FindDict("params");
    static const base::NoDestructor<base::DictValue> kEmpty;
    HandleEvent(*method, params ? *params : *kEmpty);
    return;
  }
  if (std::optional<int> id = dict.FindInt("id")) {
    HandleCommandResult(*id, dict.FindDict("result"));
  }
}

void DumpTabSession::AgentHostClosed(content::DevToolsAgentHost* agent_host) {
  agent_host_.reset();
  dump_domain_.clear();
  script_command_to_url_.clear();
  html_command_to_url_.clear();
  pending_html_.clear();
}

std::string DumpTabSession::GetTypeForMetrics() {
  return "Other";
}

void DumpTabSession::HandleEvent(const std::string& method,
                                 const base::DictValue& params) {
  if (dump_domain_.empty()) {
    return;
  }

  if (method == "Debugger.scriptParsed") {
    const std::string* script_id = params.FindString("scriptId");
    if (!script_id) {
      return;
    }
    const std::string* url = params.FindString("url");
    const int id = next_command_id_;
    script_command_to_url_[id] = url ? *url : std::string();
    base::DictValue src_params;
    src_params.Set("scriptId", *script_id);
    SendCommand("Debugger.getScriptSource", std::move(src_params));
    return;
  }

  if (method == "Network.responseReceived") {
    const base::DictValue* response = params.FindDict("response");
    const std::string* request_id = params.FindString("requestId");
    if (!response || !request_id) {
      return;
    }
    const std::string* mime = response->FindString("mimeType");
    if (!mime || mime->find("html") == std::string::npos) {
      return;  // Only capture HTML documents; JS comes from the debugger.
    }
    const std::string* url = response->FindString("url");
    pending_html_[*request_id] = url ? *url : std::string();
    return;
  }

  if (method == "Network.loadingFinished") {
    const std::string* request_id = params.FindString("requestId");
    if (!request_id) {
      return;
    }
    auto it = pending_html_.find(*request_id);
    if (it == pending_html_.end()) {
      return;
    }
    const int id = next_command_id_;
    html_command_to_url_[id] = it->second;
    pending_html_.erase(it);
    base::DictValue body_params;
    body_params.Set("requestId", *request_id);
    SendCommand("Network.getResponseBody", std::move(body_params));
    return;
  }
}

void DumpTabSession::HandleCommandResult(int id,
                                         const base::DictValue* result) {
  if (dump_domain_.empty() || !result) {
    script_command_to_url_.erase(id);
    html_command_to_url_.erase(id);
    return;
  }

  auto script_it = script_command_to_url_.find(id);
  if (script_it != script_command_to_url_.end()) {
    const std::string url = script_it->second;
    script_command_to_url_.erase(script_it);
    if (const std::string* source = result->FindString("scriptSource")) {
      recorder_->AddCapture(dump_domain_, "js", url, *source);
    }
    return;
  }

  auto html_it = html_command_to_url_.find(id);
  if (html_it != html_command_to_url_.end()) {
    const std::string url = html_it->second;
    html_command_to_url_.erase(html_it);
    const std::string* body = result->FindString("body");
    if (!body) {
      return;
    }
    if (result->FindBool("base64Encoded").value_or(false)) {
      std::string decoded;
      if (base::Base64Decode(*body, &decoded)) {
        recorder_->AddCapture(dump_domain_, "html", url, std::move(decoded));
      }
    } else {
      recorder_->AddCapture(dump_domain_, "html", url, *body);
    }
  }
}

}  // namespace trace_tools
