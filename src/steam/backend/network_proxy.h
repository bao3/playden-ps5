// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <curl/curl.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace playden::steam {

enum class ProxyMode { kOff, kHttp, kSocks5 };

// One saved proxy. The password is wiped when this value is destroyed.
struct NetworkProxy {
  ProxyMode mode = ProxyMode::kOff;
  std::string host;
  int port = 0;
  std::string username;
  std::string password;
  bool global = false;

  NetworkProxy() = default;
  NetworkProxy(const NetworkProxy& other);
  NetworkProxy(NetworkProxy&& other) noexcept;
  NetworkProxy& operator=(const NetworkProxy& other);
  NetworkProxy& operator=(NetworkProxy&& other) noexcept;
  ~NetworkProxy();

  bool operator==(const NetworkProxy& other) const;
};

enum class ProxyScope {
  kDownload,  // content downloads, the speed test, and Playden/runtime kit updates
  kExternal,  // every other external transfer; used only when Global is on
};

enum class ProxyBindResult { kDirect, kBound, kRejected, kCanceled };

ProxyMode ParseProxyMode(std::string_view text);
const char* ProxyModeName(ProxyMode mode);

// port_text is the decimal port from preferences. 0 means missing or unusable.
NetworkProxy MakeNetworkProxy(std::string_view mode, std::string host, std::string port_text,
                              std::string username, std::string password, bool global);

// "http host:port" or "socks5 host:port". Never the username or password.
std::string ProxyLogLabel(const NetworkProxy& proxy);

bool ProxyLoopbackUrl(const std::string& url);

// Replaces the process-wide snapshot. Identical values keep the generation.
void SetNetworkProxy(NetworkProxy proxy);
NetworkProxy CurrentNetworkProxy();
std::uint64_t NetworkProxyGeneration();

// Sets CURLOPT_PROXY on easy. kRejected means the proxy was required and could not be
// used: the caller fails the transfer and does not connect directly. kDirect leaves an
// empty proxy. Loopback destinations are always kDirect.
ProxyBindResult BindProxy(CURL* easy, ProxyScope scope, const std::string& url,
                          const std::atomic<bool>& cancel);

}  // namespace playden::steam
