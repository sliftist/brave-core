/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_TRACE_WRITER_H_
#define BRAVE_BROWSER_TRACE_TOOLS_TRACE_WRITER_H_

#include <cstdint>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/values.h"

namespace trace_tools {

// Appends records to a single ".trace" file in an extensible binary container:
//
//   [8]  magic "BRTRACE\0"
//   [4]  u32 header_json_length (LE)
//   [N]  header JSON (utf8)
//   records*, each:
//     [4] u32 record_length (LE)  -- bytes following this field for the record
//     [1] u8  record_type
//     [4] u32 meta_json_length (LE)
//     [M] meta JSON (utf8)
//     [B] body bytes
//
// All multi-byte integers are little-endian. Instances live entirely on a
// single blocking sequence and are meant to be owned via base::SequenceBound:
// the constructor creates the parent directory, opens the file and writes the
// header; the mutating methods take ownership of their arguments so they can be
// posted cheaply from the UI thread.
class TraceWriter {
 public:
  static constexpr uint8_t kRecordHttpRequest = 1;
  static constexpr uint8_t kRecordHttpResponse = 2;
  static constexpr uint8_t kRecordWsFrame = 3;
  static constexpr uint8_t kRecordTraceEnd = 4;

  // Hard cap on a single trace file (5 GiB). Once reached, AppendRecord() drops
  // further records.
  static constexpr int64_t kMaxTraceBytes = int64_t{5} * 1024 * 1024 * 1024;

  // Creates parent dirs, opens `path` and writes the magic + header. If the
  // file cannot be opened the writer is left invalid and all appends are no-ops.
  TraceWriter(base::FilePath path, base::DictValue header);
  ~TraceWriter();

  TraceWriter(const TraceWriter&) = delete;
  TraceWriter& operator=(const TraceWriter&) = delete;

  // Appends one record. `body` may be empty. Dropped once the size cap is
  // reached, after Finalize(), or on IO error.
  void AppendRecord(uint8_t type,
                    base::DictValue meta,
                    std::vector<uint8_t> body);

  // Writes a terminal kRecordTraceEnd record (with running totals) and flushes.
  void Finalize(base::DictValue end_meta);

 private:
  bool WriteAll(base::span<const uint8_t> data);

  base::File file_;
  int64_t total_bytes_ = 0;
  bool cap_reached_ = false;
  bool finalized_ = false;
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_TRACE_WRITER_H_
