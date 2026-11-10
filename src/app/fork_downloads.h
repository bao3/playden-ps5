#pragma once
// fork_downloads.h - Proxy and mirror settings in their own file.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
//
// preferences.conf stays on the upstream schema. These settings live in downloads.conf.
// A preferences.conf written by an older fork build (version 13-15, download_proxy_* keys)
// is copied here once and rewritten as upstream version 12 so the shared fields still load.
// An upstream version 13 or 14 file is left alone.

#include "platform/storage.h"
#include "update/github_mirror.h"

#include <optional>
#include <string>
#include <string_view>

namespace playden {

struct ForkDownloads {
  std::string proxy_mode = "off";
  std::string proxy_host;
  std::string proxy_port;
  std::string proxy_username;
  std::string proxy_password;
  bool proxy_global = false;
  bool github_mirror = false;
  std::string github_mirror_base;
  std::string github_api_base;
};

ForkDownloads DefaultForkDownloads();

// True when the text is an older fork preferences file, not an upstream version 13 or 14 file.
bool LooksLikeForkPreferences(std::string_view text);
std::optional<ForkDownloads> ForkDownloadsFromPreferences(std::string_view text);
// Upstream version 12 containing the shared fields. Empty when the text is not a fork file.
std::string PreferencesV12FromFork(std::string_view text);

// Missing file: the build default (mirror off when no address was injected).
ForkDownloads LoadForkDownloads(Storage& storage);
Result<void> SaveForkDownloads(Storage& storage, const ForkDownloads& downloads);
// Rewrites a fork-shaped preferences.conf and stores its proxy fields when downloads.conf
// does not already exist. Returns whether a fork-shaped preferences file was rewritten.
bool MigrateForkPreferences(Storage& storage);

void ApplyForkDownloads(const ForkDownloads& downloads);

}  // namespace playden
