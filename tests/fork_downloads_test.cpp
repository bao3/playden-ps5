#include "app/fork_downloads.h"
#include "platform/storage.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}
}  // namespace

int main() {
  const char* upstream =
      "version=14\nreduce_motion=0\ntitle_store_suffix=0\ncloud_saves_auto=0\nsteam_sign_in_password=0\n"
      "downloads_slow_network=0\nsteam_stay_signed_in=1\nbeta_updates=0\ndownload_server=\nweb_access=1\n"
      "content_language=english\nhide_run_once_help=0\nhide_launcher_help=0\nruntime_prompt_kit=\n"
      "recipe_votes=0\nsharing_explained=0\nshare_name=\nreports_explained=0\nshare_as=none\nask_after_games=1\n";
  Check(!playden::LooksLikeForkPreferences(upstream), "an upstream version 14 file is not a fork file");

  const char* fork15 =
      "version=15\nreduce_motion=1\ntitle_store_suffix=0\ncloud_saves_auto=0\nsteam_sign_in_password=1\n"
      "downloads_slow_network=1\nsteam_stay_signed_in=1\nbeta_updates=0\ndownload_server=example\nweb_access=1\n"
      "content_language=english\nhide_run_once_help=0\nhide_launcher_help=0\nruntime_prompt_kit=\n"
      "download_proxy_mode=http\ndownload_proxy_host=proxy.example\ndownload_proxy_port=8080\n"
      "download_proxy_username=user\ndownload_proxy_password=secret\ndownload_proxy_global=0\n"
      "github_mirror=1\ngithub_mirror_base=https://mirror.example\n"
      "github_api_base=https://mirror.example/https://api.github.com\n";
  Check(playden::LooksLikeForkPreferences(fork15), "a fork version 15 file is recognized");
  const auto extracted = playden::ForkDownloadsFromPreferences(fork15);
  Check(extracted && extracted->proxy_mode == "http" && extracted->proxy_host == "proxy.example" &&
            !extracted->proxy_global && extracted->github_mirror &&
            extracted->github_mirror_base == "https://mirror.example" &&
            extracted->github_api_base == "https://mirror.example/https://api.github.com",
        "fork fields are copied out of preferences");
  const std::string v12 = playden::PreferencesV12FromFork(fork15);
  Check(v12.rfind("version=12\n", 0) == 0 && v12.find("reduce_motion=1\n") != std::string::npos &&
            v12.find("beta_updates=0\n") != std::string::npos && v12.find("download_server=example\n") != std::string::npos &&
            v12.find("download_proxy") == std::string::npos && v12.find("github_") == std::string::npos,
        "shared preferences become upstream version 12 without proxy keys");

  char pattern[] = "/tmp/playden-fork-downloads-XXXXXX";
  if (!mkdtemp(pattern)) return 1;
  const std::string root = pattern;
  {
    playden::Storage storage(root);
    Check(storage.WriteAtomic("preferences.conf", playden::Bytes(fork15, fork15 + std::char_traits<char>::length(fork15))).ok(),
          "writes a fork preferences file");
    Check(playden::MigrateForkPreferences(storage), "migrates the fork file");
    const auto prefs = storage.Read("preferences.conf");
    const auto saved = storage.Read("downloads.conf");
    Check(prefs.ok() && std::string(prefs.value().begin(), prefs.value().end()).rfind("version=12\n", 0) == 0,
          "preferences.conf is upstream version 12 after migration");
    Check(saved.ok() && std::string(saved.value().begin(), saved.value().end()).find("proxy_host=proxy.example\n") != std::string::npos,
          "downloads.conf keeps the saved proxy");
    Check(!playden::MigrateForkPreferences(storage), "an upstream file is not migrated again");
    const auto loaded = playden::LoadForkDownloads(storage);
    Check(loaded.proxy_mode == "http" && loaded.github_mirror_base == "https://mirror.example",
          "a saved mirror address is kept");
  }
  {
    char empty_pattern[] = "/tmp/playden-fork-empty-XXXXXX";
    if (!mkdtemp(empty_pattern)) return 1;
    const std::string empty_root = empty_pattern;
    playden::Storage storage(empty_root);
    const auto defaults = playden::LoadForkDownloads(storage);
    Check(defaults.proxy_mode == "off" && !defaults.proxy_global, "proxy defaults off");
    const auto built = playden::DefaultForkDownloads();
    Check(defaults.github_mirror == built.github_mirror && defaults.github_mirror_base == built.github_mirror_base &&
              defaults.github_api_base == built.github_api_base,
          "an unset build does not invent a mirror address");
    rmdir(empty_root.c_str());
  }
  std::remove((root + "/preferences.conf").c_str());
  std::remove((root + "/downloads.conf").c_str());
  rmdir(root.c_str());
  if (failures) {
    std::fprintf(stderr, "%d checks failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
