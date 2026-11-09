# GitHub release mirror and fork update source

Approved 2026-11-09 for the fork https://github.com/bao3/playden-ps5. This is not an upstream change. `v0.11.0-beta.9001` stays as published.

Live checks of `https://gh.heibang.club` (from this cloud VM, not from a PS5) showed:

- `https://gh.heibang.club/github.com/<owner>/<repo>/releases/download/<tag>/<file>` returns the same bytes as GitHub, including `Range` `206`.
- `https://gh.heibang.club/https://github.com/...` returns HTTP 429 and an HTML page. That form is not used.
- `api.github.com` is not proxied (HTTP 404). Release metadata stays on GitHub's API.
- The mirror serves the file itself. It does not redirect to `release-assets.githubusercontent.com`.

## Settings › Downloads

Two rows after Global proxy:

| Row | Behavior |
| --- | --- |
| GitHub mirror | On by default in this build. Saved even while a downloads proxy is on. |
| Mirror address | HTTPS base URL. Default `https://gh.heibang.club`. No userinfo, path, query, or fragment. |

The mirror applies only when it is on, the address is valid, and Proxy is Off. HTTP or SOCKS5, including a non-global proxy, leaves the saved mirror in place and does not use it. The row says why. Turning Proxy back to Off uses the saved mirror again.

An invalid address cannot be turned on. Release downloads then fail closed. They are not fetched from `github.com` instead.

## What is rewritten

Only at request time, and only this shape:

`https://github.com/<owner>/<repo>/releases/download/<tag>/<file>`

becomes:

`https://<mirror-host>/github.com/<owner>/<repo>/releases/download/<tag>/<file>`

The stored asset URL stays the GitHub URL. `api.github.com` is never rewritten. `*.githubusercontent.com` is never rewritten. A mirror response that redirects anywhere except the same mirror host is refused, and that other host is not requested.

TLS verification stays on. The SHA-256 compared with a file is the `digest` GitHub's API returned for that asset. `playden-update.json` is checked against that digest before it is parsed. A checksum is not taken from the mirror and then trusted for the same bytes.

`Authorization`, `Cookie`, `Proxy-Authorization`, `X-Api-Key`, and `X-Auth-Token` are removed from mirror requests.

HTML (`Content-Type: text/html`, or a `200`/`429` body that is an HTML page) fails the download immediately. HTTP 429 that is not HTML retries at most the existing attempt limit, waiting for `Retry-After` and never more than 60 seconds. There is no direct GitHub retry after that.

## Update source

The app looks for its own updates at `https://api.github.com/repos/bao3/playden-ps5/releases` and links notes to `https://github.com/bao3/playden-ps5/releases`. That address is compiled in. A saved settings file cannot point the check back at `leolobato/playden-ps5`.

GitHub's `/releases/latest` omits prereleases. This fork publishes prereleases. While the installed version itself has a beta number, the check reads the release list and offers the newest release, including a newer beta. The saved Beta updates switch is not changed. A stable install still uses that switch.

The API request is direct unless Global proxy is on. Release files, including `playden-update.json` and the runtime kit zip, use the mirror when the mirror is in effect, and the downloads proxy when Proxy is HTTP or SOCKS5.

## Preferences

`preferences.conf` version 14 appends:

```
github_mirror=0|1
github_mirror_base=
```

Versions 1–13 still load. A version 13 file keeps its proxy mode, host, port, username, password, and global flag. The new mirror fields use this build's defaults (on, `https://gh.heibang.club`). `beta_updates` is not rewritten.

## Package

`PLAYDEN_PRERELEASE=beta.9002`. The runtime kit pin stays 0.20. The kit zip is the published 0.20 file, checked against `959acf877a82b2711454fd62ba4a9a7c0476ee24627e92511283aeb54ace5a46`. The new tag is `v0.11.0-beta.9002`. The `v0.11.0-beta.9001` release is not modified.

## What a console must still prove

No part of this was run on a PS5. The mirror was not opened from a console network. `sceNetResolver` and a real Steam login were not exercised.
