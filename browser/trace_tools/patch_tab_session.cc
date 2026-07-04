/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/patch_tab_session.h"

#include <optional>
#include <string_view>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/trace_tools/patch_recorder.h"
#include "brave/browser/trace_tools/trace_domain_util.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace trace_tools {

namespace {

// Response headers we must not echo back verbatim: the body is handed to
// fulfillRequest already decoded and possibly resized, so a stale length or
// encoding would corrupt the response.
bool IsDroppedHeader(std::string_view name) {
  return base::EqualsCaseInsensitiveASCII(name, "content-length") ||
         base::EqualsCaseInsensitiveASCII(name, "content-encoding");
}

base::ListValue FilterHeaders(const base::ListValue& headers) {
  base::ListValue out;
  for (const base::Value& entry : headers) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* name = dict->FindString("name");
    if (name && IsDroppedHeader(*name)) {
      continue;
    }
    out.Append(entry.Clone());
  }
  return out;
}

}  // namespace

PatchTabSession::PendingResponse::PendingResponse() = default;
PatchTabSession::PendingResponse::PendingResponse(PendingResponse&&) = default;
PatchTabSession::PendingResponse& PatchTabSession::PendingResponse::operator=(
    PendingResponse&&) = default;
PatchTabSession::PendingResponse::~PendingResponse() = default;

PatchTabSession::PatchTabSession(PatchRecorder* recorder,
                                 content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents), recorder_(recorder) {
  UpdateCurrentDomain();
}

PatchTabSession::~PatchTabSession() {
  Detach();
}

void PatchTabSession::UpdateCurrentDomain() {
  if (!web_contents()) {
    current_domain_.clear();
    return;
  }
  current_domain_ = GetTraceDomain(web_contents()->GetLastCommittedURL());
}

void PatchTabSession::Evaluate() {
  const bool should_patch =
      !current_domain_.empty() && recorder_->HasRulesForDomain(current_domain_);
  if (should_patch) {
    if (!agent_host_) {
      Attach();
    } else if (attached_domain_ != current_domain_) {
      Detach();
      Attach();
    }
  } else if (agent_host_) {
    Detach();
  }
}

void PatchTabSession::Attach() {
  if (agent_host_ || !web_contents()) {
    return;
  }
  agent_host_ = content::DevToolsAgentHost::GetOrCreateFor(web_contents());
  if (!agent_host_->AttachClient(this)) {
    agent_host_.reset();
    return;
  }
  attached_domain_ = current_domain_;

  // Pause document and script responses so we can rewrite their bodies before
  // the renderer parses/compiles them.
  base::ListValue patterns;
  for (const char* type : {"Document", "Script"}) {
    base::DictValue pattern;
    pattern.Set("urlPattern", "*");
    pattern.Set("resourceType", type);
    pattern.Set("requestStage", "Response");
    patterns.Append(std::move(pattern));
  }
  base::DictValue params;
  params.Set("patterns", std::move(patterns));
  SendCommand("Fetch.enable", std::move(params));
}

void PatchTabSession::Detach() {
  if (!agent_host_) {
    return;
  }
  agent_host_->DetachClient(this);
  agent_host_.reset();
  attached_domain_.clear();
  body_command_.clear();
}

void PatchTabSession::SendCommand(const std::string& method,
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
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&PatchTabSession::DispatchToAgent,
                                weak_factory_.GetWeakPtr(), std::move(json)));
}

void PatchTabSession::DispatchToAgent(std::string json) {
  if (!agent_host_) {
    return;
  }
  agent_host_->DispatchProtocolMessage(this, base::as_byte_span(json));
}

void PatchTabSession::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted()) {
    return;
  }
  UpdateCurrentDomain();
  recorder_->EnsureRulesForDomain(current_domain_);
  Evaluate();
}

void PatchTabSession::DispatchProtocolMessage(
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

void PatchTabSession::AgentHostClosed(content::DevToolsAgentHost* agent_host) {
  agent_host_.reset();
  attached_domain_.clear();
  body_command_.clear();
}

std::string PatchTabSession::GetTypeForMetrics() {
  return "Other";
}

void PatchTabSession::HandleEvent(const std::string& method,
                                  const base::DictValue& params) {
  if (method != "Fetch.requestPaused") {
    return;
  }
  const std::string* request_id = params.FindString("requestId");
  if (!request_id) {
    return;
  }

  // A response that failed upstream can't be fulfilled; let it continue as-is.
  // Anything not paused at the response stage likewise just continues.
  std::optional<int> status = params.FindInt("responseStatusCode");
  if (params.FindString("responseErrorReason") || !status) {
    base::DictValue cont;
    cont.Set("requestId", *request_id);
    SendCommand("Fetch.continueRequest", std::move(cont));
    return;
  }

  std::string url;
  if (const base::DictValue* request = params.FindDict("request")) {
    if (const std::string* u = request->FindString("url")) {
      url = *u;
    }
  }
  const std::string request_domain = GetTraceDomain(GURL(url));
  if (request_domain.empty()) {
    base::DictValue cont;
    cont.Set("requestId", *request_id);
    SendCommand("Fetch.continueResponse", std::move(cont));
    return;
  }

  PendingResponse pending;
  pending.request_id = *request_id;
  pending.request_domain = request_domain;
  pending.response_code = *status;
  if (const std::string* phrase = params.FindString("responseStatusText")) {
    pending.response_phrase = *phrase;
  }
  if (const base::ListValue* headers = params.FindList("responseHeaders")) {
    pending.response_headers = headers->Clone();
  }

  const int id = next_command_id_;
  body_command_[id] = std::move(pending);
  base::DictValue body_params;
  body_params.Set("requestId", *request_id);
  SendCommand("Fetch.getResponseBody", std::move(body_params));
}

void PatchTabSession::HandleCommandResult(int id,
                                          const base::DictValue* result) {
  auto it = body_command_.find(id);
  if (it == body_command_.end()) {
    return;  // Not a getResponseBody reply we're tracking.
  }
  PendingResponse pending = std::move(it->second);
  body_command_.erase(it);

  auto continue_unmodified = [&] {
    base::DictValue cont;
    cont.Set("requestId", pending.request_id);
    SendCommand("Fetch.continueResponse", std::move(cont));
  };

  const std::string* body = result ? result->FindString("body") : nullptr;
  if (!body) {
    continue_unmodified();
    return;
  }

  std::string decoded;
  if (result->FindBool("base64Encoded").value_or(false)) {
    if (!base::Base64Decode(*body, &decoded)) {
      continue_unmodified();
      return;
    }
  } else {
    decoded = *body;
  }

  std::optional<std::string> patched =
      recorder_->ApplyPatches(pending.request_domain, decoded);
  if (!patched) {
    continue_unmodified();
    return;
  }

  base::DictValue fulfill;
  fulfill.Set("requestId", pending.request_id);
  fulfill.Set("responseCode", pending.response_code);
  fulfill.Set("responseHeaders", FilterHeaders(pending.response_headers));
  fulfill.Set("body", base::Base64Encode(*patched));
  if (!pending.response_phrase.empty()) {
    fulfill.Set("responsePhrase", pending.response_phrase);
  }
  SendCommand("Fetch.fulfillRequest", std::move(fulfill));
}

}  // namespace trace_tools
