// SPDX-License-Identifier: GPL-3.0-or-later
#include "steam/backend/network_proxy.h"

#include "steam/backend/native_network.h"
#include "steam/secret.h"

#include <mutex>

namespace playden::steam {
namespace {

std::mutex g_proxy_mutex;
NetworkProxy g_proxy;
std::uint64_t g_generation = 0;

bool DigitsAndDots(const std::string& host) {
  if (host.empty()) return false;
  for (const char c : host)
    if (!((c >= '0' && c <= '9') || c == '.')) return false;
  return true;
}

bool IPv4Literal(const std::string& host, int* first_octet) {
  if (!DigitsAndDots(host)) return false;
  int octets[4] = {};
  std::size_t index = 0;
  int value = -1;
  for (std::size_t i = 0; i <= host.size() && index < 4; ++i) {
    if (i == host.size() || host[i] == '.') {
      if (value < 0 || value > 255) return false;
      octets[index++] = value;
      value = -1;
      if (i == host.size()) break;
    } else {
      if (value < 0) value = 0;
      value = value * 10 + (host[i] - '0');
      if (value > 255) return false;
    }
  }
  if (index != 4 || host.back() == '.') return false;
  if (first_octet) *first_octet = octets[0];
  return true;
}

bool DnsName(const std::string& host) {
  if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') return false;
  std::size_t label = 0;
  for (const char c : host) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) return false;
    if (c == '.') {
      if (label == 0) return false;
      label = 0;
    } else {
      ++label;
      if (label > 63) return false;
    }
  }
  return label > 0;
}

int ParsePort(const std::string& text) {
  if (text.empty() || text.size() > 5) return 0;
  int value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return 0;
    value = value * 10 + (c - '0');
  }
  return value >= 1 && value <= 65535 ? value : 0;
}

bool ControlFree(const std::string& text, std::size_t limit) {
  if (text.size() > limit) return false;
  for (const unsigned char c : text)
    if (c < 0x20 || c == 0x7f) return false;
  return true;
}

std::string LowerHost(std::string host) {
  for (char& c : host)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return host;
}

}  // namespace

NetworkProxy::NetworkProxy(const NetworkProxy& other) = default;

NetworkProxy::NetworkProxy(NetworkProxy&& other) noexcept
    : mode(other.mode),
      host(std::move(other.host)),
      port(other.port),
      username(std::move(other.username)),
      password(std::move(other.password)),
      global(other.global) {
  other.mode = ProxyMode::kOff;
  other.port = 0;
  other.global = false;
}

NetworkProxy& NetworkProxy::operator=(const NetworkProxy& other) {
  if (this != &other) {
    ZeroizeSecret(password);
    mode = other.mode;
    host = other.host;
    port = other.port;
    username = other.username;
    password = other.password;
    global = other.global;
  }
  return *this;
}

NetworkProxy& NetworkProxy::operator=(NetworkProxy&& other) noexcept {
  if (this != &other) {
    ZeroizeSecret(password);
    mode = other.mode;
    host = std::move(other.host);
    port = other.port;
    username = std::move(other.username);
    password = std::move(other.password);
    global = other.global;
    other.mode = ProxyMode::kOff;
    other.port = 0;
    other.global = false;
  }
  return *this;
}

NetworkProxy::~NetworkProxy() { ZeroizeSecret(password); }

bool NetworkProxy::operator==(const NetworkProxy& other) const {
  return mode == other.mode && host == other.host && port == other.port && username == other.username &&
         password == other.password && global == other.global;
}

ProxyMode ParseProxyMode(std::string_view text) {
  if (text == "http") return ProxyMode::kHttp;
  if (text == "socks5") return ProxyMode::kSocks5;
  return ProxyMode::kOff;
}

const char* ProxyModeName(ProxyMode mode) {
  switch (mode) {
    case ProxyMode::kHttp: return "http";
    case ProxyMode::kSocks5: return "socks5";
    case ProxyMode::kOff: return "off";
  }
  return "off";
}

NetworkProxy MakeNetworkProxy(std::string_view mode, std::string host, std::string port_text,
                              std::string username, std::string password, bool global) {
  NetworkProxy proxy;
  proxy.mode = ParseProxyMode(mode);
  if (mode != "off" && mode != "http" && mode != "socks5") proxy.mode = ProxyMode::kOff;
  proxy.host = std::move(host);
  proxy.port = ParsePort(port_text);
  proxy.username = std::move(username);
  proxy.password = std::move(password);
  proxy.global = global;
  if (!ControlFree(proxy.host, 253) || !DnsName(proxy.host) && !IPv4Literal(proxy.host, nullptr)) proxy.host.clear();
  if (proxy.host.empty()) proxy.port = 0;
  if (!ControlFree(proxy.username, 128)) proxy.username.clear();
  if (!ControlFree(proxy.password, 256) || (proxy.password.size() && proxy.username.empty()))
    ZeroizeSecret(proxy.password);
  return proxy;
}

std::string ProxyLogLabel(const NetworkProxy& proxy) {
  if (proxy.mode == ProxyMode::kOff) return "off";
  return std::string(ProxyModeName(proxy.mode)) + " " + proxy.host + ":" + std::to_string(proxy.port);
}

bool ProxyLoopbackUrl(const std::string& url) {
  CURLU* parsed = curl_url();
  if (!parsed) return false;
  char* host = nullptr;
  bool loopback = false;
  if (curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK &&
      curl_url_get(parsed, CURLUPART_HOST, &host, 0) == CURLUE_OK && host) {
    const std::string lowered = LowerHost(host);
    int first = 0;
    loopback = lowered == "localhost" || (IPv4Literal(lowered, &first) && first == 127);
  }
  curl_free(host);
  curl_url_cleanup(parsed);
  return loopback;
}

void SetNetworkProxy(NetworkProxy proxy) {
  std::lock_guard<std::mutex> lock(g_proxy_mutex);
  if (g_proxy == proxy) return;
  g_proxy = std::move(proxy);
  ++g_generation;
}

NetworkProxy CurrentNetworkProxy() {
  std::lock_guard<std::mutex> lock(g_proxy_mutex);
  return g_proxy;
}

std::uint64_t NetworkProxyGeneration() {
  std::lock_guard<std::mutex> lock(g_proxy_mutex);
  return g_generation;
}

ProxyBindResult BindProxy(CURL* easy, ProxyScope scope, const std::string& url,
                          const std::atomic<bool>& cancel) {
  if (!easy) return ProxyBindResult::kRejected;
  if (ProxyLoopbackUrl(url)) {
    curl_easy_setopt(easy, CURLOPT_PROXY, "");
    return ProxyBindResult::kDirect;
  }
  NetworkProxy proxy = CurrentNetworkProxy();
  const bool wanted = proxy.mode != ProxyMode::kOff && (proxy.global || scope == ProxyScope::kDownload);
  if (!wanted) {
    curl_easy_setopt(easy, CURLOPT_PROXY, "");
    return ProxyBindResult::kDirect;
  }
  if (cancel.load(std::memory_order_acquire)) return ProxyBindResult::kCanceled;
  int first = 0;
  const bool ipv4 = IPv4Literal(proxy.host, &first);
  if (proxy.port < 1 || proxy.port > 65535 || proxy.host.empty() || !(ipv4 || DnsName(proxy.host)) ||
      (ipv4 && first == 0) || (!proxy.password.empty() && proxy.username.empty()))
    return ProxyBindResult::kRejected;
  std::string address;
  const auto resolved = internal::ResolveProxyAddress(proxy.host, 5000, cancel, &address);
  if (resolved == internal::NativeResolveError::kCanceled) return ProxyBindResult::kCanceled;
  if (resolved != internal::NativeResolveError::kNone || address.empty()) return ProxyBindResult::kRejected;
  const std::string endpoint = address + ":" + std::to_string(proxy.port);
  const bool tls = url.compare(0, 8, "https://") == 0 || url.compare(0, 6, "wss://") == 0;
  bool ok = true;
  if (proxy.mode == ProxyMode::kHttp) {
    const std::string value = "http://" + endpoint;
    ok = curl_easy_setopt(easy, CURLOPT_PROXYTYPE, static_cast<long>(CURLPROXY_HTTP)) == CURLE_OK &&
         curl_easy_setopt(easy, CURLOPT_PROXY, value.c_str()) == CURLE_OK &&
         curl_easy_setopt(easy, CURLOPT_HTTPPROXYTUNNEL, tls ? 1L : 0L) == CURLE_OK;
  } else if (proxy.mode == ProxyMode::kSocks5) {
    ok = curl_easy_setopt(easy, CURLOPT_PROXYTYPE, static_cast<long>(CURLPROXY_SOCKS5_HOSTNAME)) == CURLE_OK &&
         curl_easy_setopt(easy, CURLOPT_PROXY, endpoint.c_str()) == CURLE_OK &&
         curl_easy_setopt(easy, CURLOPT_HTTPPROXYTUNNEL, 0L) == CURLE_OK;
  } else {
    ok = false;
  }
  if (ok && !proxy.username.empty()) {
    ok = curl_easy_setopt(easy, CURLOPT_PROXYUSERNAME, proxy.username.c_str()) == CURLE_OK &&
         curl_easy_setopt(easy, CURLOPT_PROXYPASSWORD, proxy.password.c_str()) == CURLE_OK;
    if (ok && proxy.mode == ProxyMode::kHttp)
      ok = curl_easy_setopt(easy, CURLOPT_PROXYAUTH, static_cast<long>(CURLAUTH_BASIC)) == CURLE_OK;
  }
  if (!ok) return ProxyBindResult::kRejected;
  return ProxyBindResult::kBound;
}

}  // namespace playden::steam
