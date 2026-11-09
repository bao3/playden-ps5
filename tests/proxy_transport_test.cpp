// SPDX-License-Identifier: GPL-3.0-or-later
#include "steam/backend/http_client.h"
#include "steam/backend/network_proxy.h"
#include "steam/backend/ws_client.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace playden::steam;

namespace {

std::map<std::string, std::string> Load(const std::string& path) {
  std::ifstream in(path);
  assert(in);
  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(in, line)) {
    const auto split = line.find('=');
    assert(split != std::string::npos);
    values.emplace(line.substr(0, split), line.substr(split + 1));
  }
  return values;
}

std::string Events(const std::string& path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

NetworkProxy HttpProxy(int port, bool global, const std::string& user = {}, const std::string& password = {}) {
  NetworkProxy proxy;
  proxy.mode = ProxyMode::kHttp;
  proxy.host = "127.0.0.1";
  proxy.port = port;
  proxy.username = user;
  proxy.password = password;
  proxy.global = global;
  return proxy;
}

NetworkProxy SocksProxy(int port, bool global, const std::string& user = {}, const std::string& password = {}) {
  NetworkProxy proxy = HttpProxy(port, global, user, password);
  proxy.mode = ProxyMode::kSocks5;
  return proxy;
}

HttpResponse Get(HttpClient& client, const std::string& url, bool download, bool plain = false) {
  HttpRequest request;
  request.url = url;
  request.download_proxy = download;
  request.allow_plain_http = plain;
  request.timeout_ms = 5000;
  std::atomic<bool> cancel{false};
  return client.Perform(request, cancel);
}

}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const auto config = Load(argv[1]);
  const auto log = config.at("log");
  const int origin = std::stoi(config.at("origin"));
  const std::string url = "https://download.invalid:" + std::to_string(origin) + "/ok";
  HttpClient client(config.at("ca"));
  std::atomic<bool> no_cancel{false};

  SetNetworkProxy(HttpProxy(std::stoi(config.at("http")), false));
  auto direct = Get(client, url, false);
  assert(direct.error == HttpError::kNameResolutionFailed);
  assert(Events(log).find("http connect") == std::string::npos);

  auto downloaded = Get(client, url, true);
  assert(downloaded.ok() && downloaded.status_code == 200);
  assert(std::string(downloaded.body.begin(), downloaded.body.end()) == "fixture-ok");
  assert(Events(log).find("http connect download.invalid:" + std::to_string(origin)) != std::string::npos);
  assert(downloaded.proxy_label.find("secret") == std::string::npos);

  const auto before_reject = Events(log).size();
  SetNetworkProxy(HttpProxy(1, false));
  auto missed = Get(client, url, true);
  assert(missed.error == HttpError::kProxyFailed);
  assert(HttpFailureName(missed).find("proxy_failed") != std::string::npos);
  assert(HttpFailureName(missed).find("proxypass") == std::string::npos);
  assert(Events(log).substr(before_reject).find("origin") == std::string::npos);

  SetNetworkProxy(HttpProxy(std::stoi(config.at("http_auth")), true, "proxyuser", "wrong-pass"));
  auto denied = Get(client, url, false);
  assert(denied.error == HttpError::kProxyFailed);
  assert(Events(log).find("http-auth auth-rejected") != std::string::npos);

  SetNetworkProxy(HttpProxy(std::stoi(config.at("http_auth")), true, "proxyuser", "proxypass"));
  auto allowed = Get(client, url, false);
  assert(allowed.ok() && allowed.status_code == 200);

  SetNetworkProxy(SocksProxy(std::stoi(config.at("socks")), false));
  auto socks = Get(client, url, true);
  assert(socks.ok() && socks.status_code == 200);
  assert(Events(log).find("socks domain download.invalid") != std::string::npos);
  assert(Events(log).find("socks ip ") == std::string::npos);

  SetNetworkProxy(SocksProxy(std::stoi(config.at("socks_auth")), true, "proxyuser", "wrong-pass"));
  auto socks_denied = Get(client, url, false);
  assert(socks_denied.error == HttpError::kProxyFailed);
  assert(Events(log).find("socks-auth auth-rejected") != std::string::npos);

  SetNetworkProxy(HttpProxy(std::stoi(config.at("http")), true));
  auto redirect = Get(client, "https://download.invalid:" + std::to_string(origin) + "/redirect", false);
  assert(redirect.ok() && redirect.status_code == 302);

  SetNetworkProxy(HttpProxy(1, true));
  HttpRequest loopback;
  loopback.url = "http://127.0.0.1:" + config.at("plain") + "/ok";
  loopback.allow_plain_http = true;
  loopback.timeout_ms = 5000;
  auto local = client.Perform(loopback, no_cancel);
  assert(local.ok() && local.status_code == 200);
  assert(Events(log).find("plain") != std::string::npos);

  SetNetworkProxy(HttpProxy(std::stoi(config.at("hang")), false));
  std::atomic<bool> cancel{false};
  HttpResponse canceled;
  std::thread worker([&] {
    HttpRequest request;
    request.url = url;
    request.download_proxy = true;
    request.timeout_ms = 8000;
    canceled = client.Perform(request, cancel);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  cancel.store(true);
  worker.join();
  assert(canceled.error == HttpError::kCanceled);

  SetNetworkProxy(HttpProxy(std::stoi(config.at("http")), false));
  std::atomic<int> ok_count{0};
  std::vector<std::thread> workers;
  for (int i = 0; i < 4; ++i) {
    workers.emplace_back([&] {
      HttpClient each(config.at("ca"));
      auto response = Get(each, url, true);
      if (response.ok() && response.status_code == 200) ok_count.fetch_add(1);
    });
  }
  for (auto& worker : workers) worker.join();
  assert(ok_count.load() == 4);

  SetNetworkProxy(HttpProxy(std::stoi(config.at("http")), true));
  WebSocketClient websocket(config.at("ca"));
  const std::string ws = "wss://download.invalid:" + std::to_string(origin) + "/ws";
  assert(websocket.Connect(ws, 5000, no_cancel) == HttpError::kNone);
  std::vector<std::uint8_t> frame;
  assert(websocket.Receive(&frame, 2000, no_cancel) == HttpError::kNone);
  assert(frame.size() == 2 && frame[0] == 'o' && frame[1] == 'k');
  websocket.Close();

  SetNetworkProxy(SocksProxy(std::stoi(config.at("socks")), true));
  assert(websocket.Connect(ws, 5000, no_cancel) == HttpError::kNone);
  frame.clear();
  assert(websocket.Receive(&frame, 2000, no_cancel) == HttpError::kNone);
  assert(std::string(frame.begin(), frame.end()) == "ok");
  websocket.Close();

  SetNetworkProxy(NetworkProxy{});
  return 0;
}
