#include "app/fork_downloads.h"

#include "steam/backend/network_proxy.h"

#include <algorithm>
#include <map>
#include <string>

namespace playden {
namespace {

constexpr char kPath[] = "downloads.conf";
constexpr const char* kShared[] = {
    "reduce_motion", "title_store_suffix", "cloud_saves_auto", "steam_sign_in_password",
    "downloads_slow_network", "steam_stay_signed_in", "beta_updates", "download_server",
    "web_access", "content_language", "hide_run_once_help", "hide_launcher_help", "runtime_prompt_kit"};

std::string ToString(const Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }

std::map<std::string, std::string> Lines(std::string_view text) {
  std::map<std::string, std::string> fields;
  std::size_t start = 0;
  while (start < text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    const auto line = text.substr(start, end - start);
    const auto split = line.find('=');
    if (split != std::string_view::npos) fields.emplace(std::string(line.substr(0, split)), std::string(line.substr(split + 1)));
    start = end + (end < text.size() ? 1 : 0);
    if (end == text.size()) break;
  }
  return fields;
}

std::string OneLine(std::string value) {
  value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
  value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
  return value;
}

}  // namespace

ForkDownloads DefaultForkDownloads() {
  ForkDownloads downloads;
  const auto mirror = update::BuildGithubMirrorDefaults();
  downloads.github_mirror = mirror.enabled;
  downloads.github_mirror_base = mirror.base;
  downloads.github_api_base = mirror.api_base;
  return downloads;
}

bool LooksLikeForkPreferences(std::string_view text) {
  const auto fields = Lines(text);
  const auto version = fields.find("version");
  if (version == fields.end() || (version->second != "13" && version->second != "14" && version->second != "15"))
    return false;
  return fields.count("download_proxy_mode") != 0 && fields.count("recipe_votes") == 0;
}

std::optional<ForkDownloads> ForkDownloadsFromPreferences(std::string_view text) {
  if (!LooksLikeForkPreferences(text)) return std::nullopt;
  const auto fields = Lines(text);
  ForkDownloads downloads = DefaultForkDownloads();
  const auto take = [&](const char* key, std::string* out) {
    const auto found = fields.find(key);
    if (found != fields.end()) *out = found->second;
  };
  take("download_proxy_mode", &downloads.proxy_mode);
  take("download_proxy_host", &downloads.proxy_host);
  take("download_proxy_port", &downloads.proxy_port);
  take("download_proxy_username", &downloads.proxy_username);
  take("download_proxy_password", &downloads.proxy_password);
  if (const auto global = fields.find("download_proxy_global"); global != fields.end())
    downloads.proxy_global = global->second == "1";
  if (const auto mirror = fields.find("github_mirror"); mirror != fields.end())
    downloads.github_mirror = mirror->second == "1";
  take("github_mirror_base", &downloads.github_mirror_base);
  take("github_api_base", &downloads.github_api_base);
  if (downloads.proxy_mode != "http" && downloads.proxy_mode != "socks5") downloads.proxy_mode = "off";
  return downloads;
}

std::string PreferencesV12FromFork(std::string_view text) {
  if (!LooksLikeForkPreferences(text)) return {};
  const auto fields = Lines(text);
  std::string out = "version=12\n";
  for (const char* key : kShared) {
    const auto found = fields.find(key);
    out += key;
    out += '=';
    out += found == fields.end() ? "" : found->second;
    out += '\n';
  }
  return out;
}

ForkDownloads LoadForkDownloads(Storage& storage) {
  const auto read = storage.Read(kPath);
  if (!read.ok()) return DefaultForkDownloads();
  const auto fields = Lines(ToString(read.value()));
  if (fields.count("version") == 0 || fields.at("version") != "1") return DefaultForkDownloads();
  ForkDownloads downloads = DefaultForkDownloads();
  const auto take = [&](const char* key, std::string* out) {
    const auto found = fields.find(key);
    if (found != fields.end()) *out = found->second;
  };
  take("proxy_mode", &downloads.proxy_mode);
  take("proxy_host", &downloads.proxy_host);
  take("proxy_port", &downloads.proxy_port);
  take("proxy_username", &downloads.proxy_username);
  take("proxy_password", &downloads.proxy_password);
  if (fields.count("proxy_global")) downloads.proxy_global = fields.at("proxy_global") == "1";
  if (fields.count("github_mirror")) downloads.github_mirror = fields.at("github_mirror") == "1";
  take("github_mirror_base", &downloads.github_mirror_base);
  take("github_api_base", &downloads.github_api_base);
  if (downloads.proxy_mode != "http" && downloads.proxy_mode != "socks5") downloads.proxy_mode = "off";
  return downloads;
}

Result<void> SaveForkDownloads(Storage& storage, const ForkDownloads& downloads) {
  const auto ready = storage.EnsureRootDirectory();
  if (!ready.ok()) return ready;
  const std::string mode = downloads.proxy_mode == "http" || downloads.proxy_mode == "socks5" ? downloads.proxy_mode : "off";
  const std::string text = std::string("version=1\nproxy_mode=") + mode +
                           "\nproxy_host=" + OneLine(downloads.proxy_host) +
                           "\nproxy_port=" + OneLine(downloads.proxy_port) +
                           "\nproxy_username=" + OneLine(downloads.proxy_username) +
                           "\nproxy_password=" + OneLine(downloads.proxy_password) +
                           "\nproxy_global=" + (downloads.proxy_global ? "1" : "0") +
                           "\ngithub_mirror=" + (downloads.github_mirror ? "1" : "0") +
                           "\ngithub_mirror_base=" + OneLine(downloads.github_mirror_base) +
                           "\ngithub_api_base=" + OneLine(downloads.github_api_base) + "\n";
  return storage.WriteAtomic(kPath, Bytes(text.begin(), text.end()));
}

bool MigrateForkPreferences(Storage& storage) {
  const auto read = storage.Read("preferences.conf");
  if (!read.ok()) return false;
  const std::string text = ToString(read.value());
  const auto extracted = ForkDownloadsFromPreferences(text);
  if (!extracted) return false;
  const std::string rewritten = PreferencesV12FromFork(text);
  if (rewritten.empty()) return false;
  if (!storage.Read(kPath).ok()) {
    if (!SaveForkDownloads(storage, *extracted).ok()) return false;
  }
  return storage.WriteAtomic("preferences.conf", Bytes(rewritten.begin(), rewritten.end())).ok();
}

void ApplyForkDownloads(const ForkDownloads& downloads) {
  steam::SetNetworkProxy(steam::MakeNetworkProxy(downloads.proxy_mode, downloads.proxy_host, downloads.proxy_port,
                                                 downloads.proxy_username, downloads.proxy_password,
                                                 downloads.proxy_global));
  update::SetGithubMirror({downloads.github_mirror, downloads.github_mirror_base, downloads.github_api_base});
}

}  // namespace playden
