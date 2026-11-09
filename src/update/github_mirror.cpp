#include "update/github_mirror.h"

#include <mutex>

namespace playden::update {
namespace {

std::mutex g_mirror_mutex;
GithubMirrorSettings g_mirror;

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

bool DnsLabelOk(std::string_view host) {
  if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.' || host.find('.') == std::string_view::npos)
    return false;
  std::size_t label = 0;
  for (const unsigned char c : host) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) return false;
    if (c == '.') {
      if (label == 0) return false;
      label = 0;
    } else if (++label > 63) return false;
  }
  return label > 0;
}

bool IPv4(std::string_view host, int* octets) {
  int values[4] = {};
  int index = 0;
  int value = -1;
  for (std::size_t i = 0; i <= host.size() && index < 4; ++i) {
    if (i == host.size() || host[i] == '.') {
      if (value < 0 || value > 255) return false;
      values[index++] = value;
      value = -1;
      if (i == host.size()) break;
    } else if (host[i] < '0' || host[i] > '9') {
      return false;
    } else {
      if (value < 0) value = 0;
      value = value * 10 + (host[i] - '0');
      if (value > 255) return false;
    }
  }
  if (index != 4 || host.back() == '.') return false;
  if (octets)
    for (int i = 0; i < 4; ++i) octets[i] = values[i];
  return true;
}

bool ForbiddenHost(std::string_view host) {
  if (host == "localhost" || host == "github.com" || host == "api.github.com") return true;
  const std::string content = "githubusercontent.com";
  if (host == content || (host.size() > content.size() && host.compare(host.size() - content.size(), content.size(), content) == 0 &&
                          host[host.size() - content.size() - 1] == '.'))
    return true;
  const std::string github = ".github.com";
  if (host.size() > github.size() && host.compare(host.size() - github.size(), github.size(), github) == 0) return true;
  int octets[4] = {};
  if (!IPv4(host, octets)) return false;
  if (octets[0] == 0 || octets[0] == 127) return true;
  return octets[0] == 169 && octets[1] == 254;
}

struct Origin {
  bool ok = false;
  std::string origin;  // https://host[:port], no trailing slash
  std::string host;
  int port = 443;
};

Origin ParseBase(std::string_view base) {
  Origin parsed;
  const std::string prefix = "https://";
  if (base.size() < prefix.size() || base.compare(0, prefix.size(), prefix) != 0) return parsed;
  std::string_view rest = base.substr(prefix.size());
  if (rest.empty() || rest.find_first_of("@/?#\\ ") != std::string_view::npos) {
    if (!rest.empty() && rest.back() == '/' && rest.find_first_of("@?#\\ ") == std::string_view::npos) rest.remove_suffix(1);
    else return parsed;
  }
  if (rest.empty() || rest.back() == ':') return parsed;
  std::string host(rest);
  int port = 443;
  const auto colon = host.rfind(':');
  if (colon != std::string::npos) {
    const std::string port_text = host.substr(colon + 1);
    host.resize(colon);
    if (port_text.empty() || port_text.size() > 5) return parsed;
    int value = 0;
    for (const char c : port_text) {
      if (c < '0' || c > '9') return parsed;
      value = value * 10 + (c - '0');
    }
    if (value < 1 || value > 65535) return parsed;
    port = value;
  }
  host = Lower(host);
  int ignored[4] = {};
  if (!(DnsLabelOk(host) || IPv4(host, ignored)) || ForbiddenHost(host)) return parsed;
  parsed.ok = true;
  parsed.host = host;
  parsed.port = port;
  parsed.origin = "https://" + host;
  if (port != 443) parsed.origin += ":" + std::to_string(port);
  return parsed;
}

bool SegmentOk(std::string_view segment) {
  if (segment.empty() || segment == "." || segment == "..") return false;
  for (const unsigned char c : segment) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                    c == '_' || c == '-' || c == '+';
    if (!ok) return false;
  }
  return true;
}

// owner/repo/releases/download/tag/file, nothing else.
bool ReleasePath(std::string_view url, std::string* path) {
  const std::string prefix = "https://github.com/";
  if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) return false;
  const std::string_view rest = url.substr(prefix.size());
  if (rest.find_first_of("?#\\") != std::string_view::npos || rest.back() == '/') return false;
  std::string_view segments[6];
  std::size_t count = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= rest.size(); ++i) {
    if (i != rest.size() && rest[i] != '/') continue;
    if (count == 6) return false;
    segments[count++] = rest.substr(start, i - start);
    start = i + 1;
  }
  if (count != 6 || segments[2] != "releases" || segments[3] != "download") return false;
  for (const auto& segment : segments)
    if (!SegmentOk(segment)) return false;
  *path = std::string(rest);
  return true;
}

bool HeaderNameIs(std::string_view name, std::string_view expected) {
  if (name.size() != expected.size()) return false;
  for (std::size_t i = 0; i < name.size(); ++i) {
    const char c = name[i] >= 'A' && name[i] <= 'Z' ? static_cast<char>(name[i] - 'A' + 'a') : name[i];
    if (c != expected[i]) return false;
  }
  return true;
}

struct ApiBase {
  bool ok = false;
  std::string normalized;
  std::string origin;
};

ApiBase ParseApiBase(std::string_view base) {
  ApiBase parsed;
  if (!base.empty() && base.back() == '/') base.remove_suffix(1);
  const std::string marker = "/https://api.github.com";
  if (base.size() <= marker.size() || base.compare(base.size() - marker.size(), marker.size(), marker) != 0) return parsed;
  const Origin origin = ParseBase(base.substr(0, base.size() - marker.size()));
  if (!origin.ok) return parsed;
  parsed.ok = true;
  parsed.origin = origin.origin;
  parsed.normalized = origin.origin + marker;
  return parsed;
}

bool GithubApiUrl(std::string_view url) {
  const std::string prefix = "https://api.github.com/";
  if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) return false;
  return url.find_first_of("\\# ") == std::string_view::npos;
}

}  // namespace

bool ValidGithubMirrorBase(std::string_view base) { return ParseBase(base).ok; }

bool ValidGithubApiBase(std::string_view base) { return ParseApiBase(base).ok; }

MirrorDecision DecideGithubMirror(std::string_view url, const GithubMirrorSettings& mirror, bool downloads_proxy_on) {
  MirrorDecision decision;
  decision.url = std::string(url);
  std::string path;
  const bool release = ReleasePath(url, &path);
  const bool api = GithubApiUrl(url);
  if (!release && !api) return decision;
  if (!mirror.enabled || downloads_proxy_on) return decision;
  if (release) {
    const Origin origin = ParseBase(mirror.base);
    if (!origin.ok) {
      decision.action = MirrorAction::kReject;
      decision.url.clear();
      return decision;
    }
    decision.action = MirrorAction::kRewrite;
    decision.origin = origin.origin;
    decision.url = origin.origin + "/" + std::string(url);
    return decision;
  }
  const ApiBase api_base = ParseApiBase(mirror.api_base);
  if (!api_base.ok) {
    decision.action = MirrorAction::kReject;
    decision.url.clear();
    return decision;
  }
  const std::string root = "https://api.github.com";
  decision.action = MirrorAction::kRewrite;
  decision.origin = api_base.origin;
  decision.url = api_base.normalized + std::string(url.substr(root.size()));
  return decision;
}

void StripSensitiveHeaders(std::vector<std::pair<std::string, std::string>>& headers) {
  std::vector<std::pair<std::string, std::string>> kept;
  kept.reserve(headers.size());
  for (auto& header : headers) {
    const std::string_view name = header.first;
    if (HeaderNameIs(name, "authorization") || HeaderNameIs(name, "proxy-authorization") ||
        HeaderNameIs(name, "cookie") || HeaderNameIs(name, "cookie2") || HeaderNameIs(name, "x-api-key") ||
        HeaderNameIs(name, "x-auth-token"))
      continue;
    kept.push_back(std::move(header));
  }
  headers.swap(kept);
}

void SetGithubMirror(GithubMirrorSettings mirror) {
  std::lock_guard<std::mutex> lock(g_mirror_mutex);
  g_mirror = std::move(mirror);
}

GithubMirrorSettings CurrentGithubMirror() {
  std::lock_guard<std::mutex> lock(g_mirror_mutex);
  return g_mirror;
}

}  // namespace playden::update
