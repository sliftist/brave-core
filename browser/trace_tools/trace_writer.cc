/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/trace_writer.h"

#include <array>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/numerics/byte_conversions.h"

namespace trace_tools {

namespace {

constexpr uint8_t kMagic[8] = {'B', 'R', 'T', 'R', 'A', 'C', 'E', '\0'};

std::string SerializeJson(const base::DictValue& dict) {
  std::string json;
  base::JSONWriter::Write(dict, &json);
  return json;
}

}  // namespace

TraceWriter::TraceWriter(base::FilePath path, base::DictValue header) {
  base::CreateDirectory(path.DirName());
  file_.Initialize(path,
                   base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!file_.IsValid()) {
    return;
  }

  const std::string header_json = SerializeJson(header);
  const std::array<uint8_t, 4> header_len =
      base::U32ToLittleEndian(header_json.size());

  if (!WriteAll(base::span(kMagic)) || !WriteAll(base::span(header_len)) ||
      !WriteAll(base::as_byte_span(header_json))) {
    file_.Close();
  }
}

TraceWriter::~TraceWriter() = default;

bool TraceWriter::WriteAll(base::span<const uint8_t> data) {
  if (data.empty()) {
    return true;
  }
  if (!file_.WriteAtCurrentPosAndCheck(data)) {
    return false;
  }
  total_bytes_ += static_cast<int64_t>(data.size());
  return true;
}

void TraceWriter::AppendRecord(uint8_t type,
                               base::DictValue meta,
                               std::vector<uint8_t> body) {
  if (!file_.IsValid() || cap_reached_ || finalized_) {
    return;
  }

  const std::string meta_json = SerializeJson(meta);
  // record_length counts everything after the record_length field itself:
  // type(1) + meta_len(4) + meta + body.
  const int64_t record_len = 1 + 4 +
                             static_cast<int64_t>(meta_json.size()) +
                             static_cast<int64_t>(body.size());

  if (total_bytes_ + 4 + record_len > kMaxTraceBytes) {
    cap_reached_ = true;
    return;
  }

  const std::array<uint8_t, 4> record_len_le =
      base::U32ToLittleEndian(static_cast<uint32_t>(record_len));
  const std::array<uint8_t, 1> type_byte = {type};
  const std::array<uint8_t, 4> meta_len_le =
      base::U32ToLittleEndian(meta_json.size());

  if (!WriteAll(base::span(record_len_le)) || !WriteAll(base::span(type_byte)) ||
      !WriteAll(base::span(meta_len_le)) ||
      !WriteAll(base::as_byte_span(meta_json)) || !WriteAll(base::span(body))) {
    // Partial write: treat as capped to stop further appends.
    cap_reached_ = true;
  }
}

void TraceWriter::Finalize(base::DictValue end_meta) {
  if (!file_.IsValid() || finalized_) {
    return;
  }
  // Force the end record through even if the cap was hit, so readers always see
  // a terminal marker; temporarily clear the cap flag for this one write.
  const bool was_capped = cap_reached_;
  cap_reached_ = false;
  end_meta.Set("total_bytes", static_cast<double>(total_bytes_));
  end_meta.Set("cap_reached", was_capped);
  AppendRecord(kRecordTraceEnd, std::move(end_meta), {});
  finalized_ = true;
  file_.Flush();
}

}  // namespace trace_tools
