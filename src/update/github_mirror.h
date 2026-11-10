#pragma once
// github_mirror.h - Rewrite GitHub release downloads onto a configured HTTPS mirror.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "playden_github_mirror_base.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace playden::update {

// Empty unless the build was given PLAYDEN_GITHUB_MIRROR_BASE_URL.
inline constexpr const char* kConfiguredGithubMirrorBase = PLAYDEN_GITHUB_MIRROR_BASE_URL;

struct GithubMirrorDefaults {
  bool enabled = false;
  std::string base;
  std::string api_base;
};

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

// An empty or rejected address leaves the mirror off. A valid origin turns it on and derives the
// API address as that origin plus the full https://api.github.com URL. Nothing substitutes another host.
inline GithubMirrorDefaults MirrorDefaultsFor(std::string_view configured) {
  GithubMirrorDefaults out;
  if (!ValidGithubMirrorBase(configured)) return out;
  std::string base(configured);
  if (!base.empty() && base.back() == '/') base.pop_back();
  out.enabled = true;
  out.base = std::move(base);
  out.api_base = out.base + "/https://api.github.com";
  return out;
}

inline GithubMirrorDefaults BuildGithubMirrorDefaults() { return MirrorDefaultsFor(kConfiguredGithubMirrorBase); }

// downloads_proxy_on is true when Proxy is HTTP or SOCKS5, whether or not Global proxy is on.
// Release downloads and api.github.com URLs are then left unchanged. A release download becomes
// <origin>/ + the full https://github.com URL. An API URL becomes <api base> + the API path and query.
MirrorDecision DecideGithubMirror(std::string_view url, const GithubMirrorSettings& mirror,
                                  bool downloads_proxy_on);

void StripSensitiveHeaders(std::vector<std::pair<std::string, std::string>>& headers);

void SetGithubMirror(GithubMirrorSettings mirror);
GithubMirrorSettings CurrentGithubMirror();

}  // namespace playden::update
