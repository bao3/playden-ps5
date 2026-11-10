# Cell ID experiment

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the Cell ID from a successful CM logon in the current session and in the saved session, and use that value for install and settings-page content-server lists.

**Architecture:** `CmClient::Session` already decodes field 7 of `CMsgClientLogonResponse`. A successful logon stores that value on the shared CM session (or on the client when it has no shared session) and notifies one handler. The handler updates `StoredSession::cell_id` and saves only when the value changed. Install and the settings list call `DirectoryCell(live, stored)`, which prefers the live value, including 0, and otherwise uses the saved value or 0. Failed logons do not notify and a dropped shared session forgets the live value.

**Tech Stack:** Existing C++20 Playden Steam client, ctest executables, no SteamKit.

## Global Constraints

- Branch `CellID` only. Baseline commit `4c75a80eb54cba5d704464b92f3e116086e2f8b5`. Do not commit to `main`, do not retarget releases or tags, do not touch `sale-ID` or `playden-cellid-experiment`.
- Do not import the fork's proxy, mirror, or update changes.
- Do not change HTTPS-versus-plain filtering or fallback, TLS verification, CDN auth tokens, or the Steam library.
- Do not hard-code a country, Cell ID, vendor host, or IP.
- Do not log tokens, depot keys, manifest request codes, credentials, or full authenticated URLs.
- A directory listing or a CDN root request is not a chunk download. The settings speed test's later `Measure` step downloads real chunk URLs; `List` does not.
- Cell ID 0 from a failed login is not evidence that Cell ID caused a missing server list. That hypothesis stays untested on a mainland network.

---

### Task 1: Failing tests

**Files:**
- Modify: `tests/cm_client_test.cpp`
- Modify: `tests/cdn_client_test.cpp`
- Modify: `tests/download_server_source_test.cpp`
- Modify: `tests/session_store_test.cpp`

- [x] **Step 1: Write tests**

Cover:

- `DirectoryCell` uses a live value, including 0, before a saved value, and uses 0 when both are absent.
- `ApplyLoggedOnCell` sets a missing cell, replaces 7 with 0, and does not rewrite an equal value.
- A successful scripted logon with cell 42 reports `LoggedOnCell()==42` and calls the handler once. A refused logon does not call it. After that refusal the shared session reports no cell.
- A later successful logon with cell 0 replaces 42.
- `PersistLoggedOnCell` saves 42 over a missing cell and saves 0 over 42. It does not save when the stored cell is already that value. It fails closed when no session is loaded.
- `CdnClient::directory_summary()` reports `cell`, `listed`, `https`, `plain`, and `kept` without host names.
- The settings list requests `cell_id=42` when its cell source returns 42, and still requests `cell_id=0` when no source is set.
- Session store save/load replaces an absent cell with 7 and then with 0.

- [x] **Step 2: Run the new tests and record the failure**

Run the `cm_client` and `cdn_client` targets. Expected: compile failure because `LoggedOnCell`, `directory_summary`, and `SetCellSource` do not exist yet.

### Task 2: Minimal implementation

**Files:**
- Modify: `src/steam/backend/cm_client.h`
- Modify: `src/steam/backend/cm_client.cpp`
- Modify: `src/steam/backend/cdn_client.h`
- Modify: `src/steam/backend/cdn_client.cpp`
- Modify: `src/steam/backend/depot_installer.cpp`
- Modify: `src/steam/backend/download_server_source.h`
- Modify: `src/steam/backend/download_server_source.cpp`
- Modify: `src/steam/backend/steam_install_runner.cpp`
- Modify: `src/steam/download_server_service.h`
- Modify: `src/steam/download_server_service.cpp`
- Modify: `src/homebrew/host.cpp`
- Modify: `src/homebrew/ps5.cpp`

- [x] **Step 1: Publish the learned cell**

`Session::cell_id()` returns the decoded cell only when `logon_` exists and `eresult == kEResultOk`. `Open` calls `NoteCell` only on `CmError::kNone`. `NoteCell` stores the value on the shared session under the lock already held by `Acquire`, or on the client when there is no shared session. The handler runs only when the stored live value changes. `Drop` clears the live value. `LoggedOnCell` returns it only while that shared session is still usable.

```cpp
inline std::uint32_t DirectoryCell(std::optional<std::uint32_t> live,
                                   std::optional<std::uint32_t> stored) {
  return live ? *live : stored.value_or(0);
}
inline bool ApplyLoggedOnCell(StoredSession& session, std::uint32_t cell) {
  if (session.cell_id && *session.cell_id == cell) return false;
  session.cell_id = cell;
  return true;
}
```

`PersistLoggedOnCell` loads the repository, applies the cell, and saves only when it changed. No loaded session or a failed load/save returns failure and writes nothing.

- [x] **Step 2: Use one cell for both directory callers**

The depot installer passes `DirectoryCell(cm.LoggedOnCell(), session.cell_id)` into `CdnClient::Servers`. The settings source uses `SetCellSource`. Host and PS5 wire that source to the live cell, then the repository. Both wire `SetLoggedOnCellHandler` to `PersistLoggedOnCell`.

- [x] **Step 3: Sanitized diagnostics**

Successful CM log line gains `cell=<number>` beside the existing result. It is not printed on failure. `CdnClient::directory_summary()` is `cell=<n> listed=<n> https=<n> plain=<n> kept=https|plain|none`. Install logs it as step `directory`. The settings list logs it as step `server-list` when a log function is set. No host names are added.

- [x] **Step 4: Re-run the tests**

Expected: the new assertions pass, and `cm_client`, `cdn_client`, `download_server_source`, `session_store`, and `depot_installer` still pass.

### Task 3: Record what was not run

- [x] Build the steam test targets this environment can configure. Record the command and result.
- [x] State that no PS5, no Steam account, and no mainland network were used.
- [ ] Direct retest on a mainland machine, proxy off: sign in, read `steam-cm.log` for `step=logon result=ok cell=`, then an install or settings list log for the same `cell=` plus `listed`, `https`, `plain`, and `kept`. Restart and list servers before the next logon; the directory cell should match the saved value. A CDN root HTTP status does not prove a chunk URL works.

## Self-review

- Spec coverage: success, restart persistence, install, settings list, missing, 0, update, re-login, failed logon, and shared-session invalidation each have a test or a handler rule above. HTTPS policy, TLS, tokens, and proxy code are out of scope.
- The live cell is cleared in `Drop` before a new logon, so a shared session cannot keep the previous account's cell after the credentials change.
- `ApplyLoggedOnCell` treats stored 0 and live 0 as equal, so a repeated success does not rewrite the session file.

## Results

Red, before the API existed, `g++ -std=c++20 -I src -c tests/cm_client_test.cpp` failed because `DirectoryCell`, `ApplyLoggedOnCell`, `PersistLoggedOnCell`, `CellPersist`, `SetLoggedOnCellHandler`, and `CmClient::LoggedOnCell` were not declared.

Green, after the implementation, on Linux:

```
cmake --build build/homebrew-host --target cm_client_test cdn_client_test download_server_source_test session_store_test download_server_service_test
ctest --test-dir build/homebrew-host --output-on-failure -R '^(cm_client|cdn_client|download_server_source|download_server_service|session_store)$'
```

5 tests passed, 0 failed. `cm_client` took 5.32 seconds and includes the scripted logon. The host preset's UI still stops on non-macOS, so `src/homebrew/host.cpp` and `src/homebrew/ps5.cpp` were not compiled here. `depot_installer` failed in `StorageFailureEndsStats` because that test plants a file named `DATA` to block a folder; on this case-sensitive filesystem the unified folder can be `data`, so the install is not `kStorage`. That assertion does not call the new cell path.

No PS5, Steam account, or mainland network was used. A directory summary is not evidence that a chunk URL works.
