#include "update/github_mirror.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); std::exit(1); } } while (false)

namespace {
using playden::update::DecideGithubMirror;
using playden::update::GithubMirrorSettings;
using playden::update::MirrorAction;
using playden::update::StripSensitiveHeaders;

const char* kRelease = "https://github.com/bao3/playden-ps5/releases/download/v0.11.0-beta.9002/SHA256SUMS";
const char* kRewritten =
    "https://mirror.example/https://github.com/bao3/playden-ps5/releases/download/v0.11.0-beta.9002/SHA256SUMS";
const char* kApi = "https://api.github.com/repos/bao3/playden-ps5/releases?per_page=100";
const char* kApiRewritten =
    "https://mirror.example/https://api.github.com/repos/bao3/playden-ps5/releases?per_page=100";

GithubMirrorSettings Mirror() {
  GithubMirrorSettings mirror;
  mirror.enabled = true;
  mirror.base = "https://mirror.example";
  mirror.api_base = "https://mirror.example/https://api.github.com";
  return mirror;
}
}  // namespace

int main() {
  const auto mirror = Mirror();
  const auto rewritten = DecideGithubMirror(kRelease, mirror, false);
  CHECK(rewritten.action == MirrorAction::kRewrite);
  CHECK(rewritten.url == kRewritten);
  CHECK(rewritten.origin == "https://mirror.example");

  const auto slashed = DecideGithubMirror(kRelease, GithubMirrorSettings{true, "https://mirror.example/", mirror.api_base}, false);
  CHECK(slashed.action == MirrorAction::kRewrite && slashed.url == kRewritten);

  const auto port = DecideGithubMirror(kRelease, GithubMirrorSettings{true, "https://mirror.example:8443", mirror.api_base}, false);
  CHECK(port.action == MirrorAction::kRewrite);
  CHECK(port.url ==
        "https://mirror.example:8443/https://github.com/bao3/playden-ps5/releases/download/v0.11.0-beta.9002/SHA256SUMS");

  const auto api = DecideGithubMirror(kApi, mirror, false);
  CHECK(api.action == MirrorAction::kRewrite && api.url == kApiRewritten && api.origin == "https://mirror.example");
  const auto api_paused = DecideGithubMirror(kApi, mirror, true);
  CHECK(api_paused.action == MirrorAction::kKeep && api_paused.url == kApi);
  GithubMirrorSettings no_api = mirror;
  no_api.api_base.clear();
  CHECK(DecideGithubMirror(kApi, no_api, false).action == MirrorAction::kReject);
  CHECK(DecideGithubMirror(kRelease, no_api, false).action == MirrorAction::kRewrite);

  for (const char* kept : {"https://github.com/bao3/playden-ps5/releases/tag/v0.11.0-beta.9002",
                           "https://release-assets.githubusercontent.com/asset",
                           "https://objects.githubusercontent.com/github-production-release-asset/x",
                           "http://github.com/bao3/playden-ps5/releases/download/v1/a.zip"}) {
    const auto decision = DecideGithubMirror(kept, mirror, false);
    CHECK(decision.action == MirrorAction::kKeep);
    CHECK(decision.url == kept);
  }

  const auto paused = DecideGithubMirror(kRelease, mirror, true);
  CHECK(paused.action == MirrorAction::kKeep && paused.url == kRelease);

  GithubMirrorSettings off = mirror;
  off.enabled = false;
  const auto disabled = DecideGithubMirror(kRelease, off, false);
  CHECK(disabled.action == MirrorAction::kKeep && disabled.url == kRelease);

  for (const char* bad : {"http://mirror.example", "https://user:pass@mirror.example", "https://mirror.example/extra",
                          "https://mirror.example?q=1", "https://127.0.0.1", "https://localhost",
                          "https://github.com", "https://api.github.com", "https://evil.githubusercontent.com", ""}) {
    CHECK(!playden::update::ValidGithubMirrorBase(bad));
    const auto rejected = DecideGithubMirror(kRelease, GithubMirrorSettings{true, bad, {}}, false);
    CHECK(rejected.action == MirrorAction::kReject);
  }
  CHECK(playden::update::ValidGithubMirrorBase("https://mirror.example"));
  const auto injected = playden::update::MirrorDefaultsFor("https://mirror.example");
  CHECK(injected.enabled && injected.base == "https://mirror.example" &&
        injected.api_base == "https://mirror.example/https://api.github.com");
  const auto unset = playden::update::MirrorDefaultsFor("");
  CHECK(!unset.enabled && unset.base.empty() && unset.api_base.empty());
  CHECK(!playden::update::MirrorDefaultsFor("https://127.0.0.1").enabled);
  const auto built = playden::update::BuildGithubMirrorDefaults();
  if (std::string(playden::update::kConfiguredGithubMirrorBase).empty()) {
    CHECK(!built.enabled && built.base.empty() && built.api_base.empty());
  } else {
    CHECK(built.enabled && built.base == playden::update::kConfiguredGithubMirrorBase &&
          built.api_base == built.base + "/https://api.github.com");
  }
  CHECK(playden::update::ValidGithubApiBase("https://mirror.example/https://api.github.com/"));
  CHECK(!playden::update::ValidGithubApiBase("https://mirror.example/https://github.com"));
  CHECK(!playden::update::ValidGithubApiBase("https://127.0.0.1/https://api.github.com"));
  CHECK(std::string(playden::update::kUpdateLatestReleaseUrl) ==
        "https://api.github.com/repos/bao3/playden-ps5/releases/latest");
  CHECK(std::string(playden::update::kUpdateReleasesPage) == "https://github.com/bao3/playden-ps5/releases");
  CHECK(std::string(playden::update::kUpdateLatestReleaseUrl).find("leolobato") == std::string::npos);

  std::vector<std::pair<std::string, std::string>> headers = {
      {"Authorization", "Bearer secret"}, {"Cookie", "session=1"}, {"Proxy-Authorization", "Basic eA=="},
      {"X-Api-Key", "key"},               {"X-Auth-Token", "token"}, {"User-Agent", "Playden"},
      {"Accept", "*/*"},                  {"Range", "bytes=0-0"}};
  StripSensitiveHeaders(headers);
  CHECK(headers.size() == 3);
  CHECK(headers[0].first == "User-Agent" && headers[1].first == "Accept" && headers[2].first == "Range");

  std::puts("github_mirror_test: all assertions passed");
  return EXIT_SUCCESS;
}
