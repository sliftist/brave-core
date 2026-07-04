/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/tab_trace_session.h"

#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "brave/browser/trace_tools/network_trace_recorder.h"
#include "brave/browser/trace_tools/trace_domain_util.h"
#include "brave/browser/trace_tools/trace_writer.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

namespace trace_tools {

namespace {

// Per-response body cap (50 MiB); larger bodies are truncated.
constexpr size_t kMaxBodyBytes = size_t{50} * 1024 * 1024;

double NowMs() {
  return base::Time::Now().InMillisecondsFSinceUnixEpoch();
}

std::vector<uint8_t> ToBytes(std::string_view s) {
  return std::vector<uint8_t>(s.begin(), s.end());
}

}  // namespace

TabTraceSession::TabTraceSession(NetworkTraceRecorder* recorder,
                                 content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents), recorder_(recorder) {
  UpdateCurrentDomain();
}

TabTraceSession::~TabTraceSession() {
  Detach();
}

void TabTraceSession::UpdateCurrentDomain() {
  if (!web_contents()) {
    current_domain_.clear();
    return;
  }
  current_domain_ = GetTraceDomain(web_contents()->GetLastCommittedURL());
}

void TabTraceSession::Evaluate() {
  const bool should_trace =
      !current_domain_.empty() && recorder_->IsDomainTraced(current_domain_);
  if (should_trace) {
    if (!agent_host_) {
      Attach();
    } else if (trace_domain_ != current_domain_) {
      // Navigated to a different traced domain: re-attach under the new one.
      Detach();
      Attach();
    }
  } else if (agent_host_) {
    Detach();
  }
}

void TabTraceSession::Attach() {
  if (agent_host_ || !web_contents()) {
    return;
  }
  agent_host_ = content::DevToolsAgentHost::GetOrCreateFor(web_contents());
  if (!agent_host_->AttachClient(this)) {
    agent_host_.reset();
    return;
  }
  trace_domain_ = current_domain_;

  base::DictValue params;
  params.Set("maxTotalBufferSize", 100 * 1024 * 1024);
  params.Set("maxResourceBufferSize", 50 * 1024 * 1024);
  SendCommand("Network.enable", std::move(params));
}

void TabTraceSession::Detach() {
  if (!agent_host_) {
    return;
  }
  agent_host_->DetachClient(this);
  agent_host_.reset();
  trace_domain_.clear();
  body_command_to_request_.clear();
  pending_responses_.clear();
}

void TabTraceSession::SendCommand(const std::string& method,
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

void TabTraceSession::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted()) {
    return;
  }
  UpdateCurrentDomain();
  Evaluate();
}

void TabTraceSession::DispatchProtocolMessage(
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

void TabTraceSession::AgentHostClosed(content::DevToolsAgentHost* agent_host) {
  agent_host_.reset();
  trace_domain_.clear();
  body_command_to_request_.clear();
  pending_responses_.clear();
}

std::string TabTraceSession::GetTypeForMetrics() {
  return "Other";
}

void TabTraceSession::HandleEvent(const std::string& method,
                                  const base::DictValue& params) {
  if (trace_domain_.empty()) {
    return;
  }

  if (method == "Network.requestWillBeSent") {
    const base::DictValue* request = params.FindDict("request");
    const std::string* request_id = params.FindString("requestId");
    if (!request || !request_id) {
      return;
    }
    base::DictValue meta;
    meta.Set("requestId", *request_id);
    meta.Set("ts", NowMs());
    if (const std::string* url = request->FindString("url")) {
      meta.Set("url", *url);
    }
    if (const std::string* http_method = request->FindString("method")) {
      meta.Set("method", *http_method);
    }
    if (const std::string* type = params.FindString("type")) {
      meta.Set("type", *type);
    }
    if (const base::DictValue* headers = request->FindDict("headers")) {
      meta.Set("headers", headers->Clone());
    }
    recorder_->WriteRecord(trace_domain_, TraceWriter::kRecordHttpRequest,
                           std::move(meta), {});
    return;
  }

  if (method == "Network.responseReceived") {
    const base::DictValue* response = params.FindDict("response");
    const std::string* request_id = params.FindString("requestId");
    if (!response || !request_id) {
      return;
    }
    base::DictValue meta;
    meta.Set("requestId", *request_id);
    meta.Set("ts", NowMs());
    if (const std::string* url = response->FindString("url")) {
      meta.Set("url", *url);
    }
    if (std::optional<int> status = response->FindInt("status")) {
      meta.Set("status", *status);
    }
    if (const std::string* mime = response->FindString("mimeType")) {
      meta.Set("mimeType", *mime);
    }
    if (const base::DictValue* headers = response->FindDict("headers")) {
      meta.Set("headers", headers->Clone());
    }
    pending_responses_[*request_id] = std::move(meta);
    return;
  }

  if (method == "Network.loadingFinished") {
    const std::string* request_id = params.FindString("requestId");
    if (!request_id || !pending_responses_.contains(*request_id)) {
      return;
    }
    const int id = next_command_id_;
    body_command_to_request_[id] = *request_id;
    base::DictValue body_params;
    body_params.Set("requestId", *request_id);
    SendCommand("Network.getResponseBody", std::move(body_params));
    return;
  }

  if (method == "Network.loadingFailed") {
    const std::string* request_id = params.FindString("requestId");
    if (!request_id) {
      return;
    }
    auto it = pending_responses_.find(*request_id);
    base::DictValue meta =
        it != pending_responses_.end() ? std::move(it->second) : base::DictValue();
    if (it != pending_responses_.end()) {
      pending_responses_.erase(it);
    }
    meta.Set("requestId", *request_id);
    meta.Set("failed", true);
    if (const std::string* error = params.FindString("errorText")) {
      meta.Set("error", *error);
    }
    recorder_->WriteRecord(trace_domain_, TraceWriter::kRecordHttpResponse,
                           std::move(meta), {});
    return;
  }

  if (method == "Network.webSocketCreated" ||
      method == "Network.webSocketClosed" ||
      method == "Network.webSocketFrameSent" ||
      method == "Network.webSocketFrameReceived") {
    const std::string* request_id = params.FindString("requestId");
    if (!request_id) {
      return;
    }
    base::DictValue meta;
    meta.Set("requestId", *request_id);
    meta.Set("ts", NowMs());
    std::vector<uint8_t> body;
    if (method == "Network.webSocketCreated") {
      meta.Set("direction", "create");
      if (const std::string* url = params.FindString("url")) {
        meta.Set("url", *url);
      }
    } else if (method == "Network.webSocketClosed") {
      meta.Set("direction", "close");
    } else {
      meta.Set("direction", method == "Network.webSocketFrameSent"
                                ? "sent"
                                : "received");
      if (const base::DictValue* response = params.FindDict("response")) {
        if (std::optional<int> opcode = response->FindInt("opcode")) {
          meta.Set("opcode", *opcode);
        }
        if (const std::string* payload = response->FindString("payloadData")) {
          body = ToBytes(*payload);
        }
      }
    }
    recorder_->WriteRecord(trace_domain_, TraceWriter::kRecordWsFrame,
                           std::move(meta), std::move(body));
    return;
  }
}

void TabTraceSession::HandleCommandResult(int id,
                                          const base::DictValue* result) {
  auto id_it = body_command_to_request_.find(id);
  if (id_it == body_command_to_request_.end()) {
    return;
  }
  const std::string request_id = id_it->second;
  body_command_to_request_.erase(id_it);

  auto pending_it = pending_responses_.find(request_id);
  if (pending_it == pending_responses_.end()) {
    return;
  }
  base::DictValue meta = std::move(pending_it->second);
  pending_responses_.erase(pending_it);

  std::vector<uint8_t> body;
  if (result) {
    const std::string* body_str = result->FindString("body");
    const std::optional<bool> base64 = result->FindBool("base64Encoded");
    if (body_str) {
      if (base64.value_or(false)) {
        std::string decoded;
        if (base::Base64Decode(*body_str, &decoded)) {
          body = ToBytes(decoded);
        }
      } else {
        body = ToBytes(*body_str);
      }
    }
  }

  if (body.size() > kMaxBodyBytes) {
    body.resize(kMaxBodyBytes);
    meta.Set("truncated", true);
  }
  meta.Set("body_bytes", static_cast<double>(body.size()));
  recorder_->WriteRecord(trace_domain_, TraceWriter::kRecordHttpResponse,
                         std::move(meta), std::move(body));
}

}  // namespace trace_tools
