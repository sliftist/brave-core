/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_TRACE_TOOLS_TRACE_DOMAIN_UTIL_H_
#define BRAVE_BROWSER_TRACE_TOOLS_TRACE_DOMAIN_UTIL_H_

#include <string>

#include "base/files/file_path.h"

class GURL;

namespace trace_tools {

// Returns the grouping key for a URL: its eTLD+1 (e.g. "example.co.uk"),
// falling back to the bare host for IP literals / single-label hosts like
// "localhost". The result is sanitized so it is always safe to use as a single
// path component (only [A-Za-z0-9._-], everything else collapsed to '_').
// Returns empty string for URLs without a host (about:, chrome:, etc.).
std::string GetTraceDomain(const GURL& url);

// Root directory for all trace-tools output: ~/browser-traces
base::FilePath GetTraceRootDir();

// ~/browser-traces/<domain>
base::FilePath GetDomainDir(const std::string& domain);

}  // namespace trace_tools

#endif  // BRAVE_BROWSER_TRACE_TOOLS_TRACE_DOMAIN_UTIL_H_
