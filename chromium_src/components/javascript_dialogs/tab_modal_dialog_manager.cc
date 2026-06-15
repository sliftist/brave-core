/* Copyright (c) 2025 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/constants/enhanced_phishing_protection.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"

namespace {

// True when the "Enhanced Phishing Protection" setting is on for this tab. When
// on, beforeunload confirmation dialogs are auto-dismissed (proceed), so a page
// cannot trap the user with a "Leave site?" prompt.
bool IsEnhancedPhishingProtectionEnabled(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }
  PrefService* prefs =
      user_prefs::UserPrefs::Get(web_contents->GetBrowserContext());
  return prefs && prefs->GetBoolean(kEnhancedPhishingProtectionEnabled);
}

}  // namespace

// If tab's web contents is not active(not foremost), it should be
// treated as hidden. This situation can happen from inactive split tab.
// Otherwise, dialog could be launched from inactive split tab.
#define BRAVE_TAB_MODAL_DIALOG_MANAGER_ON_VISIBILITY_CHANGED \
  if (visibility != content::Visibility::HIDDEN &&           \
      !delegate_->IsWebContentsForemost()) {                 \
    visibility = content::Visibility::HIDDEN;                \
  }

// Auto-proceed (no dialog) on beforeunload when Enhanced Phishing Protection
// is enabled.
#define BRAVE_TAB_MODAL_DIALOG_MANAGER_RUN_BEFORE_UNLOAD_DIALOG \
  if (IsEnhancedPhishingProtectionEnabled(web_contents)) {      \
    std::move(callback).Run(/*success=*/true, std::u16string()); \
    return;                                                     \
  }

#include <components/javascript_dialogs/tab_modal_dialog_manager.cc>

#undef BRAVE_TAB_MODAL_DIALOG_MANAGER_RUN_BEFORE_UNLOAD_DIALOG
#undef BRAVE_TAB_MODAL_DIALOG_MANAGER_ON_VISIBILITY_CHANGED

namespace javascript_dialogs {

void TabModalDialogManager::OnTabActiveStateChanged() {
  OnVisibilityChanged(web_contents()->GetVisibility());
}

}  // namespace javascript_dialogs
