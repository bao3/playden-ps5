// SPDX-License-Identifier: GPL-3.0-or-later
#include "steam/backend/network_proxy.h"

#include <atomic>
#include <cassert>
#include <curl/curl.h>
#include <string>

using namespace playden::steam;

int main() {
  assert(ParseProxyMode("off") == ProxyMode::kOff);
  assert(ParseProxyMode("http") == ProxyMode::kHttp);
  assert(ParseProxyMode("socks5") == ProxyMode::kSocks5);
  assert(ParseProxyMode("https") == ProxyMode::kOff);
  assert(ProxyLoopbackUrl("http://127.0.0.1:10101/mount"));
  assert(ProxyLoopbackUrl("https://localhost/ok"));
  assert(!ProxyLoopbackUrl("https://download.invalid/ok"));

  const auto rejected = MakeNetworkProxy("http", "203.0.113.5", "8080", "", "secret-value", true);
  assert(rejected.password.empty());
  const auto proxy = MakeNetworkProxy("socks5", "203.0.113.5", "1080", "proxyuser", "secret-value", false);
  assert(proxy.mode == ProxyMode::kSocks5 && proxy.port == 1080 && proxy.password == "secret-value");
  const auto label = ProxyLogLabel(proxy);
  assert(label == "socks5 203.0.113.5:1080");
  assert(label.find("secret-value") == std::string::npos);
  assert(label.find("proxyuser") == std::string::npos);

  const auto before = NetworkProxyGeneration();
  SetNetworkProxy(proxy);
  assert(NetworkProxyGeneration() == before + 1);
  SetNetworkProxy(CurrentNetworkProxy());
  assert(NetworkProxyGeneration() == before + 1);

  assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
  CURL* easy = curl_easy_init();
  assert(easy);
  std::atomic<bool> cancel{false};
  assert(BindProxy(easy, ProxyScope::kExternal, "http://127.0.0.1:10101/mount", cancel) ==
         ProxyBindResult::kDirect);
  assert(BindProxy(easy, ProxyScope::kExternal, "https://download.invalid/ok", cancel) ==
         ProxyBindResult::kDirect);
  assert(BindProxy(easy, ProxyScope::kDownload, "https://download.invalid/ok", cancel) ==
         ProxyBindResult::kBound);
  NetworkProxy global = proxy;
  global.global = true;
  SetNetworkProxy(global);
  assert(BindProxy(easy, ProxyScope::kExternal, "https://download.invalid/ok", cancel) ==
         ProxyBindResult::kBound);
  assert(BindProxy(easy, ProxyScope::kExternal, "http://127.0.0.1:9/direct", cancel) ==
         ProxyBindResult::kDirect);
  SetNetworkProxy(MakeNetworkProxy("http", "", "", "", "", false));
  assert(BindProxy(easy, ProxyScope::kDownload, "https://download.invalid/ok", cancel) ==
         ProxyBindResult::kRejected);
  curl_easy_cleanup(easy);
  SetNetworkProxy(NetworkProxy{});
  return 0;
}
