#include "update/asset_download.h"
#include "update/github_mirror.h"
#include "update/release.h"

#include "steam/backend/network_proxy.h"

#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <optional>

namespace playden::update {
namespace {

constexpr int kMaxRedirects = 5;

std::string Header(const steam::HttpResponse& response, const char* name) {
  for (const auto& [key, value] : response.headers)
    if (strcasecmp(key.c_str(), name) == 0) return value;
  return {};
}

bool Redirect(long status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }

std::string Host(const std::string& url) {
  const std::string scheme = "https://";
  if (url.compare(0, scheme.size(), scheme) != 0) return {};
  const auto end = url.find_first_of("/?#", scheme.size());
  std::string host = url.substr(scheme.size(), end == std::string::npos ? std::string::npos : end - scheme.size());
  // No user info or ports.
  if (host.find_first_of("@:") != std::string::npos) return {};
  for (auto& c : host) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  return host;
}

void Log(const DownloadOptions& options, const std::string& line) {
  if (options.log) options.log(line);
}

bool Cancelled(const DownloadOptions& options) { return options.cancel && options.cancel->load(); }

steam::HttpResponse Get(const DownloadOptions& options, const std::string& url, const std::string& range,
                        std::size_t limit, bool download_proxy, const std::string& accept = "*/*") {
  steam::HttpRequest request;
  request.url = url;
  request.headers = {{"User-Agent", options.user_agent}, {"Accept", accept}};
  if (!range.empty()) request.headers.emplace_back("Range", "bytes=" + range);
  StripSensitiveHeaders(request.headers);
  request.timeout_ms = 120000;
  request.max_response_bytes = limit;
  request.download_proxy = download_proxy;
  static const std::atomic<bool> never{false};
  return options.fetch(request, options.cancel ? *options.cancel : never);
}

bool Fatal(const std::string& error) { return error.rfind("blocked:", 0) == 0; }

std::string PublicError(std::string error) {
  if (Fatal(error)) error.erase(0, 8);
  if (!error.empty() && error.front() == ' ') error.erase(error.begin());
  return error;
}

bool LooksLikeHtml(const steam::HttpResponse& response) {
  std::string type = Header(response, "content-type");
  for (char& c : type)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  const auto semi = type.find(';');
  if (semi != std::string::npos) type.resize(semi);
  while (!type.empty() && (type.back() == ' ' || type.back() == '\t')) type.pop_back();
  if (type.rfind("text/html", 0) == 0) return true;
  if (response.status_code == 206 || (response.status_code != 200 && response.status_code != 429)) return false;
  const std::size_t n = std::min<std::size_t>(response.body.size(), 64);
  std::string prefix(response.body.begin(), response.body.begin() + static_cast<std::ptrdiff_t>(n));
  for (char& c : prefix)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return prefix.rfind("<!doctype html", 0) == 0 || prefix.rfind("<html", 0) == 0;
}

// nullopt when the header is absent: the caller uses its normal backoff. A date waits the cap.
std::optional<int> RetryAfterSeconds(const steam::HttpResponse& response) {
  std::string value = Header(response, "retry-after");
  std::size_t start = 0;
  while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
  std::size_t end = value.size();
  while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) --end;
  value = value.substr(start, end - start);
  if (value.empty()) return std::nullopt;
  int number = 0;
  for (const char c : value) {
    if (c < '0' || c > '9') return 60;
    number = number * 10 + (c - '0');
    if (number > 60) return 60;
  }
  return number;
}

struct PreparedUrl {
  std::string url;
  std::string error;
  bool mirror = false;
  std::string origin;
  bool fatal = false;
};

PreparedUrl PrepareUrl(const std::string& url) {
  PreparedUrl prepared;
  const bool downloads_proxy = steam::CurrentNetworkProxy().mode != steam::ProxyMode::kOff;
  const MirrorDecision decision = DecideGithubMirror(url, CurrentGithubMirror(), downloads_proxy);
  if (decision.action == MirrorAction::kReject) {
    prepared.fatal = true;
    prepared.error = url.rfind("https://api.github.com/", 0) == 0 ? "blocked: GitHub API address isn't valid"
                                                                  : "blocked: GitHub mirror address isn't valid";
    return prepared;
  }
  prepared.url = decision.url;
  if (decision.action == MirrorAction::kRewrite) {
    prepared.mirror = true;
    prepared.origin = decision.origin;
  }
  return prepared;
}

bool JsonDocument(const steam::HttpResponse& response) {
  std::string type = Header(response, "content-type");
  for (char& c : type)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  const auto semi = type.find(';');
  if (semi != std::string::npos) type.resize(semi);
  while (!type.empty() && (type.back() == ' ' || type.back() == '\t')) type.pop_back();
  if (!type.empty() && type.find("json") == std::string::npos) return false;
  std::size_t i = 0;
  while (i < response.body.size() && (response.body[i] == ' ' || response.body[i] == '\n' || response.body[i] == '\r' ||
                                      response.body[i] == '\t'))
    ++i;
  return i < response.body.size() && (response.body[i] == '{' || response.body[i] == '[');
}

bool RateLimited(const steam::HttpResponse& response) {
  if (response.status_code != 403 && response.status_code != 429) return false;
  if (Header(response, "x-ratelimit-remaining") == "0") return true;
  std::string body(response.body.begin(), response.body.end());
  for (char& c : body)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return body.find("rate limit") != std::string::npos;
}

bool AcceptUrl(const std::string& url, bool mirror, const std::string& origin, std::string* error) {
  if (mirror) {
    if (url.rfind("https://", 0) != 0 || url.rfind(origin + "/", 0) != 0) {
      *error = "blocked: mirror redirected to another host";
      return false;
    }
    return true;
  }
  if (!AllowedDownloadUrl(url)) {
    *error = "download host not allowed";
    return false;
  }
  return true;
}

std::string Describe(const steam::HttpResponse& response) {
  return "status " + std::to_string(response.status_code) + " error " + std::to_string(static_cast<int>(response.error));
}

// Follows redirects from url until a server answers the probe range. Returns the final URL.
// retry_after is set when the answer is HTTP 429 and the caller should try the same mirror again.
std::optional<std::string> Resolve(const DownloadOptions& options, std::string url, bool mirror,
                                   const std::string& origin, std::string* error, std::optional<int>* retry_after) {
  retry_after->reset();
  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    if (!AcceptUrl(url, mirror, origin, error)) return std::nullopt;
    const auto response = Get(options, url, "0-0", 64 << 10, true);
    if (Redirect(response.status_code)) {
      url = Header(response, "location");
      continue;
    }
    if (LooksLikeHtml(response)) {
      *error = "blocked: download returned a web page";
      return std::nullopt;
    }
    if (response.status_code == 429) {
      *error = "download was rate limited (" + Describe(response) + ")";
      *retry_after = RetryAfterSeconds(response);
      return std::nullopt;
    }
    if (response.status_code == 206 || response.status_code == 200) return url;
    *error = "download link failed (" + Describe(response) + ")";
    return std::nullopt;
  }
  *error = "too many redirects";
  return std::nullopt;
}

void Wait(const DownloadOptions& options, int attempt, std::optional<int> retry_after) {
  if (retry_after) {
    const int seconds = *retry_after;
    if (options.wait_seconds) options.wait_seconds(seconds);
    else
      for (int step = 0; step < seconds * 10 && !Cancelled(options); ++step) usleep(100000);
    return;
  }
  if (options.wait_before_retry) {
    options.wait_before_retry(attempt);
    return;
  }
  for (int step = 0; step < 20 * attempt && !Cancelled(options); ++step) usleep(100000);
}

}  // namespace

bool AllowedDownloadUrl(const std::string& url) {
  const std::string host = Host(url);
  if (host.empty()) return false;
  if (host == "github.com" || host == "api.github.com") return true;
  const std::string suffix = ".githubusercontent.com";
  return host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::optional<std::string> FetchSmall(const std::string& url, std::size_t limit, const DownloadOptions& options,
                                      const std::string& accept, std::string* error, bool through_download_proxy) {
  const PreparedUrl prepared = PrepareUrl(url);
  if (prepared.fatal) {
    *error = PublicError(prepared.error);
    return std::nullopt;
  }
  std::string target = prepared.url;
  const int attempts_allowed = std::max(1, options.max_attempts);
  for (int attempt = 1; attempt <= attempts_allowed; ++attempt) {
    bool rate_limited = false;
    std::optional<int> retry_after;
    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
      if (!AcceptUrl(target, prepared.mirror, prepared.origin, error)) {
        *error = PublicError(*error);
        return std::nullopt;
      }
      const auto response = Get(options, target, "", limit, through_download_proxy, accept);
      if (Redirect(response.status_code)) {
        target = Header(response, "location");
        continue;
      }
      if (LooksLikeHtml(response)) {
        *error = "blocked: download returned a web page";
        return std::nullopt;
      }
      if (response.status_code == 403 && RateLimited(response)) {
        *error = "blocked: GitHub rate limit";
        return std::nullopt;
      }
      if (response.status_code == 429) {
        *error = "download was rate limited (" + Describe(response) + ")";
        retry_after = RetryAfterSeconds(response);
        rate_limited = true;
        break;
      }
      if (response.status_code != 200 || !response.ok()) {
        *error = Describe(response);
        return std::nullopt;
      }
      const bool api = target.rfind("https://api.github.com/", 0) == 0 ||
                       target.find("/https://api.github.com/") != std::string::npos;
      if (api && !JsonDocument(response)) {
        *error = "blocked: GitHub's API did not return JSON";
        return std::nullopt;
      }
      return std::string(response.body.begin(), response.body.end());
    }
    if (!rate_limited) {
      *error = "too many redirects";
      return std::nullopt;
    }
    if (attempt == attempts_allowed) return std::nullopt;
    if (Cancelled(options)) {
      *error = "cancelled";
      return std::nullopt;
    }
    Wait(options, attempt, retry_after);
    target = prepared.url;
  }
  return std::nullopt;
}

DownloadResult DownloadAsset(const std::string& url, std::uint64_t size, const std::string& sha256,
                             const std::string& path, const DownloadOptions& options) {
  DownloadResult result;
  auto fail = [&](std::string reason) {
    result.error = std::move(reason);
    Log(options, "download " + path + " failed: " + result.error);
    return result;
  };
  if (!size) return fail("empty asset");
  // GitHub's digest is the only check on the bytes: without one, nothing is installed.
  if (!IsSha256(sha256)) return fail("no checksum for this download");
  const PreparedUrl prepared = PrepareUrl(url);
  if (prepared.fatal) return fail(PublicError(prepared.error));
  const std::string part = path + ".part";
  struct stat st{};
  std::uint64_t done = stat(part.c_str(), &st) == 0 ? static_cast<std::uint64_t>(st.st_size) : 0;
  if (done > size) {
    unlink(part.c_str());
    done = 0;
  }
  if (done) Log(options, "download " + path + " resumes at " + std::to_string(done));
  const int fd = open(part.c_str(), O_CREAT | O_WRONLY, 0666);
  if (fd < 0) return fail("cannot create " + part + " errno=" + std::to_string(errno));
  fchmod(fd, 0666);
  if (ftruncate(fd, static_cast<off_t>(done)) != 0) {
    close(fd);
    return fail("cannot resize " + part);
  }
  std::string target;
  int attempts = 0;
  while (done < size) {
    if (Cancelled(options)) {
      close(fd);
      result.cancelled = true;
      return fail("cancelled");
    }
    if (target.empty()) {
      std::string error;
      std::optional<int> retry_after;
      const auto resolved = Resolve(options, prepared.url, prepared.mirror, prepared.origin, &error, &retry_after);
      if (!resolved) {
        if (Cancelled(options)) continue;
        if (Fatal(error) || ++attempts >= options.max_attempts) { close(fd); return fail(PublicError(error)); }
        Wait(options, attempts, retry_after);
        continue;
      }
      target = *resolved;
    }
    const std::uint64_t end = std::min(size, done + options.chunk_bytes) - 1;
    const auto response = Get(options, target, std::to_string(done) + "-" + std::to_string(end),
                              static_cast<std::size_t>(end - done + 1 + 1024), true);
    // The bytes asked for, and only those: "Content-Range: bytes <done>-<end>/<size>".
    const std::string range = "bytes " + std::to_string(done) + "-" + std::to_string(end) + "/";
    if (response.status_code == 206 && response.ok() && response.body.size() == end - done + 1 &&
        Header(response, "content-range").rfind(range, 0) == 0) {
      std::size_t written = 0;
      while (written < response.body.size()) {
        const ssize_t n = pwrite(fd, response.body.data() + written, response.body.size() - written,
                                 static_cast<off_t>(done + written));
        if (n <= 0) { close(fd); return fail("write failed errno=" + std::to_string(errno)); }
        written += static_cast<std::size_t>(n);
      }
      done += response.body.size();
      attempts = 0;
      if (options.progress) options.progress(done, size);
      continue;
    }
    if (Cancelled(options)) continue;
    if (LooksLikeHtml(response)) { close(fd); return fail("download returned a web page"); }
    if (response.status_code == 429) {
      Log(options, "download chunk at " + std::to_string(done) + ": " + Describe(response));
      if (++attempts >= options.max_attempts) { close(fd); return fail("download was rate limited (" + Describe(response) + ")"); }
      Wait(options, attempts, RetryAfterSeconds(response));
      continue;
    }
    Log(options, "download chunk at " + std::to_string(done) + ": " + Describe(response));
    // An expired signed link or a server that ignores Range: ask GitHub for a new link.
    if (response.status_code == 401 || response.status_code == 403 || response.status_code == 404 ||
        response.status_code == 200 || response.status_code == 416)
      target.clear();
    if (++attempts >= options.max_attempts) { close(fd); return fail("download keeps failing (" + Describe(response) + ")"); }
    Wait(options, attempts, std::nullopt);
  }
  const bool synced = fsync(fd) == 0;
  close(fd);
  if (!synced) return fail("cannot sync " + part);
  const auto actual = options.hash_file ? options.hash_file(part) : std::nullopt;
  if (!actual) return fail("cannot read the download to check it");  // the bytes stay for a retry
  if (*actual != sha256) {
    unlink(part.c_str());
    return fail("checksum mismatch");
  }
  if (rename(part.c_str(), path.c_str()) != 0) return fail("cannot rename " + part);
  result.ok = true;
  return result;
}

}  // namespace playden::update
