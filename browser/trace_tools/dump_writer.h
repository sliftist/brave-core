/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_DUMP_WRITER_H_
#define BRAVE_BROWSER_TRACE_TOOLS_DUMP_WRITER_H_

#include <cstdint>
#include <set>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

namespace trace_tools {

// Writes captured scripts and HTML documents for one dump session as plain,
// browsable files under a timestamped directory
// (~/browser-traces/<domain>/dumps/<ts>/). Content is de-duplicated by hash so
// a script compiled on every page load is stored once. Lives on a single
// blocking sequence; owned via base::SequenceBound.
class DumpWriter {
 public:
  // Hard cap on one dump directory (5 GiB); further files are dropped.
  static constexpr int64_t kMaxDumpBytes = int64_t{5} * 1024 * 1024 * 1024;

  // Reports the running (unique file count, total bytes) after each write; run
  // on the sequence the callback was bound to (the UI thread).
  using ProgressCallback = base::RepeatingCallback<void(int, int64_t)>;

  DumpWriter(base::FilePath dump_dir, ProgressCallback progress);
  ~DumpWriter();

  DumpWriter(const DumpWriter&) = delete;
  DumpWriter& operator=(const DumpWriter&) = delete;

  // Persists one capture. `kind` is "js" or "html" (selects the extension);
  // `source_url` seeds a readable filename; identical `content` is written once.
  void AddFile(std::string kind, std::string source_url, std::string content);

 private:
  const base::FilePath dir_;
  const ProgressCallback progress_;
  std::set<std::string> seen_hashes_;
  int file_count_ = 0;
  int64_t total_bytes_ = 0;
  bool cap_reached_ = false;
  bool valid_ = false;
};

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_DUMP_WRITER_H_
