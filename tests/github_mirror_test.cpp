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
    "https://gh.heibang.club/github.com/bao3/playden-ps5/releases/download/v0.11.0-beta.9002/SHA256SUMS";

GithubMirrorSettings Mirror() {
  GithubMirrorSettings mirror;
  mirror.enabled = true;
  mirror.base = "https://gh.heibang.club";
  return mirror;
}
}  // namespace

int main() {
  const auto mirror = Mirror();
  const auto rewritten = DecideGithubMirror(kRelease, mirror, false);
  CHECK(rewritten.action == MirrorAction::kRewrite);
  CHECK(rewritten.url == kRewritten);
  CHECK(rewritten.url.find("/https://") == std::string::npos);
  CHECK(rewritten.url.find("/http://") == std::string::npos);

  const auto slashed = DecideGithubMirror(kRelease, GithubMirrorSettings{true, "https://gh.heibang.club/"}, false);
  CHECK(slashed.action == MirrorAction::kRewrite && slashed.url == kRewritten);

  const auto port = DecideGithubMirror(kRelease, GithubMirrorSettings{true, "https://mirror.example:8443"}, false);
  CHECK(port.action == MirrorAction::kRewrite);
  CHECK(port.url ==
        "https://mirror.example:8443/github.com/bao3/playden-ps5/releases/download/v0.11.0-beta.9002/SHA256SUMS");

  for (const char* kept : {"https://api.github.com/repos/bao3/playden-ps5/releases/latest",
                           "https://api.github.com/repos/bao3/playden-ps5/releases?per_page=100",
                           "https://github.com/bao3/playden-ps5/releases/tag/v0.11.0-beta.9002",
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

  for (const char* bad : {"http://gh.heibang.club", "https://user:pass@gh.heibang.club", "https://gh.heibang.club/extra",
                          "https://gh.heibang.club?q=1", "https://127.0.0.1", "https://localhost",
                          "https://github.com", "https://api.github.com", "https://evil.githubusercontent.com", ""}) {
    CHECK(!playden::update::ValidGithubMirrorBase(bad));
    const auto rejected = DecideGithubMirror(kRelease, GithubMirrorSettings{true, bad}, false);
    CHECK(rejected.action == MirrorAction::kReject);
  }
  CHECK(playden::update::ValidGithubMirrorBase("https://gh.heibang.club"));
  CHECK(std::string(playden::update::kDefaultGithubMirrorBase) == "https://gh.heibang.club");
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
