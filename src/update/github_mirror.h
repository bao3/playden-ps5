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
// The mirror's documented API base: the mirror origin, then the full https://api.github.com URL.
inline constexpr const char* kDefaultGithubApiBase = "https://gh.heibang.club/https://api.github.com";

inline constexpr const char* kUpdateLatestReleaseUrl =
    "https://api.github.com/repos/bao3/playden-ps5/releases/latest";
inline constexpr const char* kUpdateReleasesPage = "https://github.com/bao3/playden-ps5/releases";

struct GithubMirrorSettings {
  bool enabled = false;
  std::string base;
  std::string api_base;
};

enum class MirrorAction {
  kKeep,     // use the original URL
  kRewrite,  // url is the mirror URL; origin is the mirror's https host
  kReject,   // a release or API URL, and the mirror is on but unusable
};

struct MirrorDecision {
  MirrorAction action = MirrorAction::kKeep;
  std::string url;
  std::string origin;
};

// HTTPS origin only: DNS name or IPv4, optional port, no userinfo, path, query, or fragment.
// Loopback, link-local, github.com, api.github.com, and githubusercontent hosts are refused.
bool ValidGithubMirrorBase(std::string_view base);
// https://<mirror-host>/https://api.github.com with the same host rules. A trailing slash is allowed.
bool ValidGithubApiBase(std::string_view base);

// downloads_proxy_on is true when Proxy is HTTP or SOCKS5, whether or not Global proxy is on.
// Release downloads and api.github.com URLs are then left unchanged. A release download becomes
// <origin>/ + the full https://github.com URL. An API URL becomes <api base> + the API path and query.
MirrorDecision DecideGithubMirror(std::string_view url, const GithubMirrorSettings& mirror,
                                  bool downloads_proxy_on);

void StripSensitiveHeaders(std::vector<std::pair<std::string, std::string>>& headers);

void SetGithubMirror(GithubMirrorSettings mirror);
GithubMirrorSettings CurrentGithubMirror();

}  // namespace playden::update
