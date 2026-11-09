# GitHub mirror implementation plan

Spec: `docs/superpowers/specs/2026-11-09-github-mirror-design.md`. Branch: `cursor/downloads-proxy-8f6b`. Do not retag `v0.11.0-beta.9001`.

- [x] Pure rewrite tests: release URL only, `/github.com/` prefix, reject `/https://`, invalid base fails closed, HTTP/SOCKS5 mode keeps the original URL.
- [x] Download tests: mirror request, no redirect off the mirror, HTML fails once, 429 honors `Retry-After` with a 60 second cap, checksum still required, sensitive headers dropped.
- [x] Preferences version 14. Version 13 keeps every proxy field and gains the mirror defaults. `beta_updates` stays as saved.
- [x] Settings rows, saved-but-inactive subtitle, and persistence.
- [x] Updater source `bao3/playden-ps5`. A beta install discovers a newer beta with Beta updates left off, ignores an older beta, reuses an installed runtime kit, and leaves API calls on `api.github.com`.
- [x] Manifest bytes checked against the API digest.
- [x] Workflow publishes `0.11.0-beta.9002` with the same four asset names and the existing kit hash.
