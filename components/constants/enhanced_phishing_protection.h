/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_CONSTANTS_ENHANCED_PHISHING_PROTECTION_H_
#define BRAVE_COMPONENTS_CONSTANTS_ENHANCED_PHISHING_PROTECTION_H_

// When enabled (brave://settings "Enhanced Phishing Protection"):
//   - the "Leave site? / Changes you made may not be saved" beforeunload
//     confirmation dialog is auto-dismissed (proceed), so a page can't trap
//     the user on the page when they try to leave; and
//   - all popups open as tabs, so a page can't spawn separate windows.
inline constexpr char kEnhancedPhishingProtectionEnabled[] =
    "brave.enhanced_phishing_protection.enabled";

#endif  // BRAVE_COMPONENTS_CONSTANTS_ENHANCED_PHISHING_PROTECTION_H_
