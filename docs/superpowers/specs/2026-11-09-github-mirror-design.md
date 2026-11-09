# GitHub release mirror and fork update source

Approved 2026-11-09 for the fork https://github.com/bao3/playden-ps5. This is not an upstream change. `v0.11.0-beta.9001` stays as published.

Live checks of `https://gh.heibang.club` (from this cloud VM, not from a PS5) showed:

- The mirror's current instructions put the full original URL after the mirror origin: `https://gh.heibang.club/https://github.com/...` for a file and `https://gh.heibang.club/https://api.github.com` for the API.
- A 32-byte range of this fork's app zip matched GitHub's copy. The API path returned GitHub's own JSON rate-limit response (`x-github-media-type`, remaining 0), not a release document, because that shared address had used its anonymous quota.
- A matching checksum compares the file with the release details from this same mirror. It is not an independent signature from GitHub.
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

`https://<mirror-host>/https://github.com/<owner>/<repo>/releases/download/<tag>/<file>`

`https://api.github.com/<path>?<query>` becomes `<API address><path>?<query>`. The default API address is `https://gh.heibang.club/https://api.github.com`. The update check uses it only while the mirror is on and Proxy is Off.

The stored asset URL stays the GitHub URL. `*.githubusercontent.com` is not rewritten. A mirror response that redirects anywhere except the same mirror host is refused, and that other host is not requested.

TLS verification stays on. `playden-update.json` is checked against the digest named by the release details before it is parsed. When the mirror is in use, those details and the file come from the same mirror.

`Authorization`, `Cookie`, `Proxy-Authorization`, `X-Api-Key`, and `X-Auth-Token` are removed from mirror requests.

HTML (`Content-Type: text/html`, or a `200`/`429` body that is an HTML page) fails the download immediately. HTTP 429 that is not HTML retries at most the existing attempt limit, waiting for `Retry-After` and never more than 60 seconds. There is no direct GitHub retry after that.

## Update source

The app looks for its own updates at `https://api.github.com/repos/bao3/playden-ps5/releases` and links notes to `https://github.com/bao3/playden-ps5/releases`. That address is compiled in. A saved settings file cannot point the check back at `leolobato/playden-ps5`.

GitHub's `/releases/latest` omits prereleases. This fork publishes prereleases. While the installed version itself has a beta number, the check reads the release list and offers the newest release, including a newer beta. The saved Beta updates switch is not changed. A stable install still uses that switch.

While the mirror is on and Proxy is Off, the update check and release files, including `playden-update.json` and the runtime kit zip, use the mirror. An HTTP or SOCKS5 proxy leaves both on their original GitHub URLs. The API call then stays direct unless Global proxy is on. A matching checksum shows the file and the release details agree with each other. Both came from the mirror, so this is not an independent GitHub signature.

## Preferences

`preferences.conf` version 15 appends `github_api_base` after the version 14 mirror fields. Versions 1–14 still load. A version 13 file keeps its proxy fields. A version 14 file keeps the mirror choice. The API address defaults to `https://gh.heibang.club/https://api.github.com`. `beta_updates` is not rewritten.

## Package

`PLAYDEN_PRERELEASE=beta.9002`. The runtime kit pin stays 0.20. The kit zip is the published 0.20 file, checked against `959acf877a82b2711454fd62ba4a9a7c0476ee24627e92511283aeb54ace5a46`. The new tag is `v0.11.0-beta.9002`. The `v0.11.0-beta.9001` release is not modified.

## What a console must still prove

No part of this was run on a PS5. The mirror was not opened from a console network. `sceNetResolver` and a real Steam login were not exercised.
