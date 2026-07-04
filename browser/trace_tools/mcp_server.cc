/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/mcp_server.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/string_util.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "base/values.h"
#include "brave/browser/trace_tools/trace_domain_util.h"
#include "brave/browser/trace_tools/trace_writer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace trace_tools {

namespace {

constexpr char kProtocolVersion[] = "2025-06-18";
constexpr char kServerName[] = "brave-browser-traces";
constexpr char kServerVersion[] = "1.0.0";

// A session counts as "active" if it was seen within this window.
constexpr base::TimeDelta kSessionActiveWindow = base::Minutes(5);

// Cap the number of matches search_trace returns and the bytes scanned per body.
constexpr size_t kDefaultSearchLimit = 100;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("brave_trace_mcp_server", R"(
      semantics {
        sender: "Brave Trace Tools MCP Server"
        description:
          "A localhost-only HTTP server exposing recorded network traces to "
          "MCP clients (e.g. an AI agent) for listing and searching."
        trigger: "Runs while the browser is open when trace tooling is built."
        data: "Recorded network traffic previously captured to disk by the user."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting: "Only reachable from 127.0.0.1."
        policy_exception_justification: "Local developer tooling."
      })");

const char* RecordTypeName(uint8_t type) {
  switch (type) {
    case TraceWriter::kRecordHttpRequest:
      return "http_request";
    case TraceWriter::kRecordHttpResponse:
      return "http_response";
    case TraceWriter::kRecordWsFrame:
      return "ws_frame";
    case TraceWriter::kRecordTraceEnd:
      return "trace_end";
    default:
      return "unknown";
  }
}

// Opens a .trace file, validates the magic and reads the JSON header. On
// success `file` is positioned at the first record.
struct OpenTrace {
  base::File file;
  base::DictValue header;
  bool ok = false;
};

OpenTrace OpenTraceFile(const base::FilePath& path) {
  OpenTrace out;
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return out;
  }
  uint8_t magic[8];
  if (!file.ReadAtCurrentPosAndCheck(magic)) {
    return out;
  }
  static constexpr uint8_t kMagic[8] = {'B', 'R', 'T', 'R', 'A', 'C', 'E', '\0'};
  if (!std::ranges::equal(magic, kMagic)) {
    return out;
  }
  uint8_t len_buf[4];
  if (!file.ReadAtCurrentPosAndCheck(len_buf)) {
    return out;
  }
  uint32_t header_len = base::U32FromLittleEndian(len_buf);
  std::string header_json(header_len, '\0');
  if (!file.ReadAtCurrentPosAndCheck(base::as_writable_byte_span(header_json))) {
    return out;
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(header_json, base::JSON_PARSE_RFC);
  if (value && value->is_dict()) {
    out.header = std::move(*value).TakeDict();
  }
  out.file = std::move(file);
  out.ok = true;
  return out;
}

// Reads the fixed part of the next record header: record_length, type and
// meta_json_length. Returns false at EOF or on a malformed record.
bool ReadRecordHead(base::File& file,
                    uint32_t* record_len,
                    uint8_t* type,
                    uint32_t* meta_len) {
  uint8_t rl_buf[4];
  if (!file.ReadAtCurrentPosAndCheck(rl_buf)) {
    return false;
  }
  *record_len = base::U32FromLittleEndian(rl_buf);
  if (*record_len < 5) {
    return false;
  }
  uint8_t head[5];
  if (!file.ReadAtCurrentPosAndCheck(head)) {
    return false;
  }
  *type = head[0];
  *meta_len = base::U32FromLittleEndian(base::span(head).subspan<1, 4>());
  if (*meta_len > *record_len - 5) {
    return false;
  }
  return true;
}

base::DictValue SummarizeTrace(const base::FilePath& path) {
  base::DictValue result;
  OpenTrace trace = OpenTraceFile(path);
  if (!trace.ok) {
    result.Set("error", "not a valid .trace file");
    return result;
  }
  result.Set("path", path.AsUTF8Unsafe());
  result.Set("header", trace.header.Clone());

  std::map<std::string, int> counts;
  int64_t total_body = 0;
  int record_count = 0;
  while (true) {
    uint32_t record_len = 0;
    uint8_t type = 0;
    uint32_t meta_len = 0;
    if (!ReadRecordHead(trace.file, &record_len, &type, &meta_len)) {
      break;
    }
    int64_t body_len = int64_t{record_len} - 5 - meta_len;
    if (trace.file.Seek(base::File::FROM_CURRENT, meta_len + body_len) < 0) {
      break;
    }
    counts[RecordTypeName(type)]++;
    total_body += body_len;
    record_count++;
  }

  base::DictValue counts_dict;
  for (const auto& [name, n] : counts) {
    counts_dict.Set(name, n);
  }
  result.Set("records", record_count);
  result.Set("counts", std::move(counts_dict));
  result.Set("total_body_bytes", static_cast<double>(total_body));
  return result;
}

base::ListValue SearchTrace(const base::FilePath& path,
                            const std::string& query,
                            size_t limit) {
  base::ListValue matches;
  OpenTrace trace = OpenTraceFile(path);
  if (!trace.ok || query.empty()) {
    return matches;
  }
  const std::string needle = base::ToLowerASCII(query);

  int index = 0;
  while (matches.size() < limit) {
    uint32_t record_len = 0;
    uint8_t type = 0;
    uint32_t meta_len = 0;
    if (!ReadRecordHead(trace.file, &record_len, &type, &meta_len)) {
      break;
    }
    int64_t body_len = int64_t{record_len} - 5 - meta_len;

    std::string meta_json(meta_len, '\0');
    if (!trace.file.ReadAtCurrentPosAndCheck(
            base::as_writable_byte_span(meta_json))) {
      break;
    }
    std::string body;
    if (body_len > 0) {
      body.resize(static_cast<size_t>(body_len));
      if (!trace.file.ReadAtCurrentPosAndCheck(
              base::as_writable_byte_span(body))) {
        break;
      }
    }

    const bool in_meta =
        base::ToLowerASCII(meta_json).find(needle) != std::string::npos;
    const bool in_body =
        !body.empty() &&
        base::ToLowerASCII(body).find(needle) != std::string::npos;
    if (in_meta || in_body) {
      base::DictValue hit;
      hit.Set("index", index);
      hit.Set("type", RecordTypeName(type));
      hit.Set("matched_in", in_meta ? (in_body ? "meta+body" : "meta") : "body");
      std::optional<base::Value> meta_value =
          base::JSONReader::Read(meta_json, base::JSON_PARSE_RFC);
      if (meta_value && meta_value->is_dict()) {
        hit.Set("meta", std::move(*meta_value));
      }
      if (in_body) {
        size_t pos = base::ToLowerASCII(body).find(needle);
        size_t start = pos > 40 ? pos - 40 : 0;
        hit.Set("body_snippet", body.substr(start, 120));
      }
      matches.Append(std::move(hit));
    }
    index++;
  }
  return matches;
}

base::ListValue ListTraces(const std::string& domain_filter,
                           const std::string& since,
                           const std::string& until) {
  base::ListValue out;
  const base::FilePath root = GetTraceRootDir();

  base::FileEnumerator dirs(root, /*recursive=*/false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = dirs.Next(); !dir.empty(); dir = dirs.Next()) {
    const std::string domain = dir.BaseName().AsUTF8Unsafe();
    if (!domain_filter.empty() && domain != domain_filter) {
      continue;
    }
    base::FileEnumerator files(dir, /*recursive=*/false,
                               base::FileEnumerator::FILES,
                               FILE_PATH_LITERAL("*.trace"));
    for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
      const std::string name = f.BaseName().AsUTF8Unsafe();
      // Filenames are "<YYYYMMDD-HHMMSS>.trace"; compare the prefix lexically.
      const std::string started = name.substr(0, name.find(".trace"));
      if (!since.empty() && started < since) {
        continue;
      }
      if (!until.empty() && started > until) {
        continue;
      }
      base::DictValue entry;
      entry.Set("domain", domain);
      entry.Set("filename", name);
      entry.Set("path", f.AsUTF8Unsafe());
      entry.Set("started", started);
      base::File::Info info;
      if (base::GetFileInfo(f, &info)) {
        entry.Set("size_bytes", static_cast<double>(info.size));
      }
      out.Append(std::move(entry));
    }
  }
  return out;
}

std::string ToJson(const base::ValueView value) {
  std::string json;
  base::JSONWriter::WriteWithOptions(
      value, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

base::DictValue ToolDescriptor(std::string_view name,
                               std::string_view description,
                               base::DictValue schema) {
  base::DictValue tool;
  tool.Set("name", name);
  tool.Set("description", description);
  tool.Set("inputSchema", std::move(schema));
  return tool;
}

base::DictValue StringProp(std::string_view description) {
  base::DictValue prop;
  prop.Set("type", "string");
  prop.Set("description", description);
  return prop;
}

base::ListValue BuildToolsList() {
  base::ListValue tools;

  {
    base::DictValue props;
    props.Set("domain", StringProp("Optional eTLD+1 to filter by."));
    props.Set("since", StringProp("Optional inclusive lower bound on the "
                                  "trace timestamp prefix (YYYYMMDD-HHMMSS)."));
    props.Set("until", StringProp("Optional inclusive upper bound."));
    base::DictValue schema;
    schema.Set("type", "object");
    schema.Set("properties", std::move(props));
    tools.Append(ToolDescriptor(
        "list_traces",
        "List recorded network traces on disk (domain, filename, size, start).",
        std::move(schema)));
  }
  {
    base::DictValue props;
    props.Set("path", StringProp("Absolute path to a .trace file."));
    base::DictValue schema;
    schema.Set("type", "object");
    schema.Set("properties", std::move(props));
    base::ListValue required;
    required.Append("path");
    schema.Set("required", std::move(required));
    tools.Append(ToolDescriptor(
        "get_trace",
        "Summarize a trace: header plus record counts and total body bytes.",
        std::move(schema)));
  }
  {
    base::DictValue props;
    props.Set("path", StringProp("Absolute path to a .trace file."));
    props.Set("query", StringProp("Case-insensitive substring to find in "
                                  "record metadata (URLs, headers) and bodies."));
    base::DictValue schema;
    schema.Set("type", "object");
    schema.Set("properties", std::move(props));
    base::ListValue required;
    required.Append("path");
    required.Append("query");
    schema.Set("required", std::move(required));
    tools.Append(ToolDescriptor(
        "search_trace",
        "Search a trace's records for a substring; returns matching records.",
        std::move(schema)));
  }
  return tools;
}

}  // namespace

// Lives on the dedicated IO thread. Owns the socket + net::HttpServer and does
// all request handling and trace-file reading there.
class MCPServer::Core : public net::HttpServer::Delegate {
 public:
  Core(uint16_t desired_port,
       scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
       base::RepeatingCallback<void(uint16_t)> port_cb,
       base::RepeatingCallback<void(int)> session_count_cb)
      : ui_task_runner_(std::move(ui_task_runner)),
        port_cb_(std::move(port_cb)),
        session_count_cb_(std::move(session_count_cb)) {
    uint16_t bound_port = 0;
    for (uint16_t port = desired_port; port < desired_port + 16; ++port) {
      auto socket =
          std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
      int rv = socket->Listen(
          net::IPEndPoint(net::IPAddress::IPv4Localhost(), port), 5,
          /*ipv6_only=*/std::nullopt);
      if (rv == net::OK) {
        server_ = std::make_unique<net::HttpServer>(std::move(socket), this);
        bound_port = port;
        break;
      }
    }
    if (ui_task_runner_) {
      ui_task_runner_->PostTask(FROM_HERE,
                                base::BindOnce(port_cb_, bound_port));
    }
    if (server_) {
      prune_timer_.Start(FROM_HERE, base::Seconds(30),
                         base::BindRepeating(&Core::PruneSessions,
                                             base::Unretained(this)));
    }
  }

  ~Core() override = default;

  // net::HttpServer::Delegate:
  void OnConnect(int connection_id) override {}
  void OnClose(int connection_id) override {}
  void OnWebSocketRequest(int connection_id,
                          const net::HttpServerRequestInfo& info) override {
    server_->Close(connection_id);
  }
  void OnWebSocketMessage(int connection_id, std::string data) override {}

  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& info) override {
    if (info.method == "OPTIONS") {
      net::HttpServerResponseInfo response(net::HTTP_OK);
      response.AddHeader("Access-Control-Allow-Origin", "*");
      response.AddHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
      response.AddHeader("Access-Control-Allow-Headers",
                         "Content-Type, Mcp-Session-Id, MCP-Protocol-Version");
      response.SetBody("", "text/plain");
      server_->SendResponse(connection_id, response, kTrafficAnnotation);
      return;
    }
    // Endpoint is POST /mcp; everything else is rejected.
    const std::string_view path(info.path);
    if (path != "/mcp" && !path.starts_with("/mcp?")) {
      server_->Send404(connection_id, kTrafficAnnotation);
      return;
    }
    if (info.method != "POST") {
      SendJsonRpc(connection_id, net::HTTP_METHOD_NOT_ALLOWED, base::DictValue(),
                  std::string());
      return;
    }
    HandleRpc(connection_id, info);
  }

 private:
  void HandleRpc(int connection_id, const net::HttpServerRequestInfo& info) {
    std::optional<base::Value> body =
        base::JSONReader::Read(info.data, base::JSON_PARSE_RFC);
    if (!body || !body->is_dict()) {
      SendError(connection_id, base::Value(), -32700, "Parse error",
                std::string());
      return;
    }
    const base::DictValue& request = body->GetDict();
    const std::string* method = request.FindString("method");
    if (!method) {
      SendError(connection_id, base::Value(), -32600, "Invalid Request",
                std::string());
      return;
    }
    const base::Value* id = request.Find("id");
    const base::DictValue* params = request.FindDict("params");

    std::string session_id = info.GetHeaderValue("mcp-session-id");

    // Notifications (including notifications/initialized) get a bare 202.
    if (!id || method->starts_with("notifications/")) {
      TouchSession(session_id);
      net::HttpServerResponseInfo response(net::HTTP_ACCEPTED);
      response.SetBody("", "text/plain");
      server_->SendResponse(connection_id, response, kTrafficAnnotation);
      return;
    }

    if (*method == "initialize") {
      session_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
      TouchSession(session_id);
      base::DictValue result;
      const std::string* client_proto =
          params ? params->FindString("protocolVersion") : nullptr;
      result.Set("protocolVersion",
                 client_proto ? *client_proto : kProtocolVersion);
      base::DictValue caps;
      base::DictValue tools_cap;
      tools_cap.Set("listChanged", false);
      caps.Set("tools", std::move(tools_cap));
      result.Set("capabilities", std::move(caps));
      base::DictValue server_info;
      server_info.Set("name", kServerName);
      server_info.Set("version", kServerVersion);
      result.Set("serverInfo", std::move(server_info));
      SendResult(connection_id, *id, std::move(result), session_id);
      return;
    }

    TouchSession(session_id);

    if (*method == "ping") {
      SendResult(connection_id, *id, base::DictValue(), std::string());
      return;
    }
    if (*method == "tools/list") {
      base::DictValue result;
      result.Set("tools", BuildToolsList());
      SendResult(connection_id, *id, std::move(result), std::string());
      return;
    }
    if (*method == "tools/call") {
      HandleToolCall(connection_id, *id, params);
      return;
    }
    SendError(connection_id, *id, -32601, "Method not found", std::string());
  }

  void HandleToolCall(int connection_id,
                      const base::Value& id,
                      const base::DictValue* params) {
    const std::string* name = params ? params->FindString("name") : nullptr;
    const base::DictValue* args = params ? params->FindDict("arguments") : nullptr;
    if (!name) {
      SendError(connection_id, id, -32602, "Missing tool name", std::string());
      return;
    }

    base::Value payload;
    bool ok = true;
    if (*name == "list_traces") {
      payload = base::Value(ListTraces(
          args ? StringArg(args, "domain") : std::string(),
          args ? StringArg(args, "since") : std::string(),
          args ? StringArg(args, "until") : std::string()));
    } else if (*name == "get_trace") {
      const std::string path = args ? StringArg(args, "path") : std::string();
      if (path.empty()) {
        ok = false;
        payload = base::Value("path is required");
      } else {
        payload = base::Value(
            SummarizeTrace(base::FilePath::FromUTF8Unsafe(path)));
      }
    } else if (*name == "search_trace") {
      const std::string path = args ? StringArg(args, "path") : std::string();
      const std::string query = args ? StringArg(args, "query") : std::string();
      if (path.empty() || query.empty()) {
        ok = false;
        payload = base::Value("path and query are required");
      } else {
        payload = base::Value(SearchTrace(
            base::FilePath::FromUTF8Unsafe(path), query, kDefaultSearchLimit));
      }
    } else {
      SendError(connection_id, id, -32602, "Unknown tool: " + *name,
                std::string());
      return;
    }

    base::DictValue text_content;
    text_content.Set("type", "text");
    text_content.Set("text", ToJson(payload));
    base::ListValue content;
    content.Append(std::move(text_content));
    base::DictValue result;
    result.Set("content", std::move(content));
    if (!ok) {
      result.Set("isError", true);
    }
    SendResult(connection_id, id, std::move(result), std::string());
  }

  static std::string StringArg(const base::DictValue* args,
                               std::string_view key) {
    const std::string* v = args->FindString(key);
    return v ? *v : std::string();
  }

  void SendResult(int connection_id,
                  const base::Value& id,
                  base::DictValue result,
                  const std::string& session_id) {
    base::DictValue envelope;
    envelope.Set("jsonrpc", "2.0");
    envelope.Set("id", id.Clone());
    envelope.Set("result", std::move(result));
    SendJsonRpc(connection_id, net::HTTP_OK, std::move(envelope), session_id);
  }

  void SendError(int connection_id,
                 const base::Value& id,
                 int code,
                 std::string_view message,
                 const std::string& session_id) {
    base::DictValue error;
    error.Set("code", code);
    error.Set("message", message);
    base::DictValue envelope;
    envelope.Set("jsonrpc", "2.0");
    envelope.Set("id", id.Clone());
    envelope.Set("error", std::move(error));
    SendJsonRpc(connection_id, net::HTTP_OK, std::move(envelope), session_id);
  }

  void SendJsonRpc(int connection_id,
                   net::HttpStatusCode status,
                   base::DictValue envelope,
                   const std::string& session_id) {
    std::string json;
    base::JSONWriter::Write(envelope, &json);
    net::HttpServerResponseInfo response(status);
    response.AddHeader("Access-Control-Allow-Origin", "*");
    if (!session_id.empty()) {
      response.AddHeader("Mcp-Session-Id", session_id);
    }
    response.SetBody(json, "application/json");
    server_->SendResponse(connection_id, response, kTrafficAnnotation);
  }

  void TouchSession(const std::string& session_id) {
    if (session_id.empty()) {
      return;
    }
    sessions_[session_id] = base::TimeTicks::Now();
    ReportSessionCount();
  }

  void PruneSessions() {
    const base::TimeTicks now = base::TimeTicks::Now();
    std::erase_if(sessions_, [&](const auto& kv) {
      return now - kv.second > kSessionActiveWindow;
    });
    ReportSessionCount();
  }

  void ReportSessionCount() {
    const int count = static_cast<int>(sessions_.size());
    if (count == last_reported_count_) {
      return;
    }
    last_reported_count_ = count;
    if (ui_task_runner_) {
      ui_task_runner_->PostTask(FROM_HERE,
                                base::BindOnce(session_count_cb_, count));
    }
  }

  scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;
  base::RepeatingCallback<void(uint16_t)> port_cb_;
  base::RepeatingCallback<void(int)> session_count_cb_;

  std::unique_ptr<net::HttpServer> server_;
  std::map<std::string, base::TimeTicks> sessions_;
  int last_reported_count_ = -1;
  base::RepeatingTimer prune_timer_;
};

MCPServer::MCPServer(uint16_t desired_port,
                     scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
                     base::RepeatingCallback<void(uint16_t)> port_cb,
                     base::RepeatingCallback<void(int)> session_count_cb)
    : thread_(std::make_unique<base::Thread>("BraveTraceMCP")) {
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  thread_->StartWithOptions(std::move(options));
  core_ = base::SequenceBound<Core>(
      thread_->task_runner(), desired_port, std::move(ui_task_runner),
      std::move(port_cb), std::move(session_count_cb));
}

MCPServer::~MCPServer() {
  core_.Reset();
  if (thread_) {
    thread_->Stop();
  }
}

}  // namespace trace_tools
