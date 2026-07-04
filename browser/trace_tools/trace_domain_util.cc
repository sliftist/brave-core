/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/trace_tools/trace_domain_util.h"

#include "base/base_paths.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace trace_tools {

namespace {

std::string Sanitize(std::string_view host) {
  std::string out;
  out.reserve(host.size());
  for (char c : host) {
    if (base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) || c == '.' ||
        c == '-' || c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  return out;
}

}  // namespace

std::string GetTraceDomain(const GURL& url) {
  if (!url.has_host()) {
    return std::string();
  }
  std::string domain = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  // IP literals and single-label hosts (localhost) return empty; use the host.
  if (domain.empty()) {
    domain = url.host();
  }
  return Sanitize(domain);
}

base::FilePath GetTraceRootDir() {
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  return home.Append(FILE_PATH_LITERAL("browser-traces"));
}

base::FilePath GetDomainDir(const std::string& domain) {
  return GetTraceRootDir().AppendASCII(domain);
}

}  // namespace trace_tools
