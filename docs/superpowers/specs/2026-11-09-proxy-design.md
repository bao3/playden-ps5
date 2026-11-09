# Downloads proxy with optional global scope

Approved 2026-11-09 for the fork https://github.com/bao3/playden-ps5. This is not an upstream change.

## Player settings

Settings › Downloads, after Download server:

| Row | Behavior |
| --- | --- |
| Proxy | Off (default), HTTP, or SOCKS5. |
| Proxy host | IPv4 or DNS name. No scheme, userinfo, space, or newline. |
| Port | Integer 1–65535. |
| Username | Optional. At most 128 characters. No control characters. |
| Password | Optional. Masked. At most 256 characters. Requires a username. |
| Global proxy | Checkbox, default unchecked. |

Off ignores the other fields and keeps today's direct connections (`CURLOPT_PROXY` set to an empty string, so environment proxy variables stay ignored).

HTTP and SOCKS5 with an unusable host or port do not fall back to a direct connection. The transfer fails with a proxy error.

Unchecked Global proxy applies the proxy to download traffic only:

- Steam content-server list, manifests, and chunks
- Download-server speed test
- Runtime kit and Playden update fetches

Checked Global proxy also applies it to every other external libcurl call:

- Steam sign-in HTTP (`IAuthenticationService`)
- CM directory HTTP (`GetCMListForConnect`) and the CM WebSocket (`wss://…/cmsocket/`)
- Library, cloud, artwork, store details, and app-info HTTP

`127.0.0.0/8` and `localhost` stay direct, including ShadowMount `http://127.0.0.1:10101`, which keeps its own empty `CURLOPT_PROXY`.

## Transport

One process-wide snapshot (`SetNetworkProxy`) is read at the start of each `HttpClient::Perform` and each `WebSocketClient::Connect`.

- HTTP: `CURLPROXY_HTTP`. HTTPS and WSS set `CURLOPT_HTTPPROXYTUNNEL` (CONNECT). Plain HTTP is forwarded by the proxy and is still not opened directly.
- SOCKS5: `CURLPROXY_SOCKS5_HOSTNAME`, so the proxy resolves the target. Credentials use `CURLOPT_PROXYUSERNAME` and `CURLOPT_PROXYPASSWORD`, never the proxy URL.
- TLS verification stays on (`VERIFYPEER` 1, `VERIFYHOST` 2, the pinned CA bundle, TLS 1.2). An intercepting proxy fails closed.
- On PS5, do not `CURLOPT_RESOLVE` the target while a proxy is bound. Resolve only the proxy host with `sceNetResolverStartNtoa` to an IPv4 address. The console curl build has `ENABLE_IPV6` off.
- A proxy failure is `proxy_failed` in logs (curl code and proxy host:port only). Player text: "The proxy failed. Check Settings, Downloads, and try again." Downloads that retry say "resume" instead of "try again."
- No password, username, URL, or header in those logs.

`CmClient::Acquire` and the shared-session pump drop the open CM WebSocket when the proxy generation changes, so the next Steam call dials with the new snapshot. An endpoint that fails is tried again through the proxy, not directly.

## Persistence

`preferences.conf` version 13 appends, after the version 12 keys:

```
download_proxy_mode=off|http|socks5
download_proxy_host=
download_proxy_port=
download_proxy_username=
download_proxy_password=
download_proxy_global=0|1
```

Versions 1–12 still load. New fields default to Off, empty, and global unchecked. The file stays mode `0600`. A password is plaintext in that file, same as the rest of the file, and is not served by Web access.

## Fork build identity

Do not change `project(PlaydenLauncher VERSION …)` or the runtime kit pin. Configure with `PLAYDEN_PRERELEASE=beta.9001`, so the package version is `0.11.0-beta.9001`. Publish a pre-release on this fork only. Do not mark it latest. Do not overwrite an existing tag or asset.

The in-app updater still reads `leolobato/playden-ps5` releases. This build is newer than the current stable 0.10.0, so it will not offer that downgrade. A later upstream 0.11.0 stable release can replace this build. That is the existing updater, left as it is. The release notes say so.

## Release files

Match the published v0.10.0 / v0.11.0-beta.1 assets, verified from those GitHub release files:

- `playden-ps5-<version>.zip` with `FAKE77031/` at the top (signed `eboot.bin`, `sce_sys`, `sce_module/libc.prx`, helper payloads, `runtime/`, `playden-version`, `playden-package.identity`, `LICENSE`, `THIRD_PARTY_NOTICES.md`) plus `tools/console_title_deploy.py`, `tools/collect_logs.py`, `tools/console_kit_upload.py`, `README.md`, `COMPATIBILITY.md`, `LICENSE`, and `THIRD_PARTY_NOTICES.md` beside that folder.
- `playden-kit-0.20.zip` reused, not rebuilt, only if its SHA-256 is `959acf877a82b2711454fd62ba4a9a7c0476ee24627e92511283aeb54ace5a46` (manifest `1c0773b5a5a78bdfbe4be2333c429dfa2653a6b945fcbea0b1a8920e2fc7d6ee`, prefix `playden-kit-0.20/playden-kit/`).
- `playden-update.json` from `tools/write_update_manifest.py`.
- `SHA256SUMS`, three GNU lines, two spaces, not hashing itself.

## What a console must still prove

A cross-compile does not run `sceNetResolver`, the native nonblocking socket, or a real Steam login. Do not install this build on a console from this work.
