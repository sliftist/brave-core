/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/dump_writer.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/hash/sha1.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace trace_tools {

namespace {

// Short hex content hash used both for de-duplication and to disambiguate
// filenames derived from the same URL.
std::string ContentHash(std::string_view content) {
  const base::SHA1Digest digest =
      base::SHA1Hash(base::as_byte_span(content));
  return base::ToLowerASCII(base::HexEncode(digest)).substr(0, 16);
}

// Collapses anything that isn't filename-safe to '_'.
std::string SanitizeComponent(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if (base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) || c == '.' ||
        c == '-' || c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty() || out.size() > 80) {
    out.resize(std::min<size_t>(out.size(), 80));
  }
  return out;
}

// Builds a stable, readable filename from the source URL + content hash, e.g.
// "app.js.1a2b3c4d5e6f7a8b.js" or "index.html.<hash>.html".
std::string MakeFilename(const std::string& kind,
                         const std::string& source_url,
                         const std::string& hash) {
  std::string base_name;
  GURL url(source_url);
  if (url.is_valid() && url.has_path()) {
    std::string_view path = url.path();
    size_t slash = path.find_last_of('/');
    std::string_view last =
        slash == std::string_view::npos ? path : path.substr(slash + 1);
    base_name = SanitizeComponent(last);
  }
  if (base_name.empty()) {
    base_name = kind == "html" ? "index" : "inline";
  }
  const char* ext = kind == "html" ? ".html" : ".js";
  return base::StrCat({base_name, ".", hash, ext});
}

}  // namespace

DumpWriter::DumpWriter(base::FilePath dump_dir, ProgressCallback progress)
    : dir_(std::move(dump_dir)), progress_(std::move(progress)) {
  valid_ = base::CreateDirectory(dir_);
}

DumpWriter::~DumpWriter() = default;

void DumpWriter::AddFile(std::string kind,
                         std::string source_url,
                         std::string content) {
  if (!valid_ || cap_reached_ || content.empty()) {
    return;
  }

  const std::string hash = ContentHash(content);
  if (!seen_hashes_.insert(hash).second) {
    return;  // Already stored this exact content.
  }

  if (total_bytes_ + static_cast<int64_t>(content.size()) > kMaxDumpBytes) {
    cap_reached_ = true;
    return;
  }

  const base::FilePath path =
      dir_.AppendASCII(MakeFilename(kind, source_url, hash));
  if (!base::WriteFile(path, content)) {
    return;
  }

  ++file_count_;
  total_bytes_ += static_cast<int64_t>(content.size());
  if (progress_) {
    progress_.Run(file_count_, total_bytes_);
  }
}

}  // namespace trace_tools
