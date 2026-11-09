#pragma once
// github_mirror.h - Rewrite GitHub release downloads onto a configured HTTPS mirror.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace playden::update {

inline constexpr const char* kDefaultGithubMirrorBase = "https://gh.heibang.club";

inline constexpr const char* kUpdateLatestReleaseUrl =
    "https://api.github.com/repos/bao3/playden-ps5/releases/latest";
inline constexpr const char* kUpdateReleasesPage = "https://github.com/bao3/playden-ps5/releases";

struct GithubMirrorSettings {
  bool enabled = false;
  std::string base;
};

enum class MirrorAction {
  kKeep,     // use the original URL
  kRewrite,  // url is the mirror URL
  kReject,   // a release download, and the mirror is on but unusable
};

struct MirrorDecision {
  MirrorAction action = MirrorAction::kKeep;
  std::string url;
};

// HTTPS origin only: DNS name or IPv4, optional port, no userinfo, path, query, or fragment.
// Loopback, link-local, github.com, api.github.com, and githubusercontent hosts are refused.
bool ValidGithubMirrorBase(std::string_view base);

// downloads_proxy_on is true when Proxy is HTTP or SOCKS5, whether or not Global proxy is on.
// Release downloads are then left on their original URL. Other URLs are always left unchanged.
MirrorDecision DecideGithubMirror(std::string_view url, const GithubMirrorSettings& mirror,
                                  bool downloads_proxy_on);

void StripSensitiveHeaders(std::vector<std::pair<std::string, std::string>>& headers);

void SetGithubMirror(GithubMirrorSettings mirror);
GithubMirrorSettings CurrentGithubMirror();

}  // namespace playden::update
