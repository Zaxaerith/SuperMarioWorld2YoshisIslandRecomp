# RetroAchievements review evidence — 19 November 2026

USA ROM, online **Softcore** only. No Hardcore, leaderboards, forged unlocks or
local/manual unlock substitution. Live tests were performed by the contributor
with their own account and ROM, not by the automated test runner.

## Requested end-to-end flow

| Requirement | Evidence | Result / qualification |
|---|---|---|
| Password sign-in | New launcher session logs: three `method=password session_saved=1` records and game 558 loaded with 63 achievements | Live logged success |
| Secure session restore | Five earlier USA review sessions contain `method=restored-token session_saved=1` | Live logged success |
| Legitimate unlock + notification | Accepted HTTP 200 responses and two matching in-game popup captures below | Live logged success, not mock responses |
| Website visibility | Contributor previously confirmed seven legitimate unlocks visible on the RA website | Owner report, not an independent account audit |
| Sign out | `saved_session_removed=1`, repeated in the latest launcher logs | Live logged success |
| Restart after sign out | Contributor confirms restart leaves them signed out; subsequent password logins are logged | Live owner observation plus supporting log records |
| Offline / reconnect | Contributor disconnected the internet, received a sign-in error, reconnected, and successfully signed in again in the same launcher | Live owner observation; successful password login/catalog load is logged. Old build did not log the failed attempt itself |
| Shutdown | Normal launcher exits show worker cancellation/join; mocked blocked-HTTP test also passes | Live normal exit + automated cancellation evidence |

The offline test covers a real connection failure and manual retry/recovery.
It is **not** a live proof of an achievement earned offline being submitted later.
That pending-award retry path is exercised with mock HTTP. No further unlock,
progress reset or repeated playthrough is required for this review evidence.
New builds flush lifecycle records immediately and log failed sign-in method and
numeric result, without account names, passwords, tokens or HTTP response bodies.

[Sanitized live excerpts](retroachievements/live-session-excerpts.txt) retain only
relevant RA lines. Complete local logs, ROMs, saves and account exports are not
published. The five distinct accepted IDs found in these review logs are 1300,
186901, 186926, 1305 and 5962. This is not presented as seven newly verified IDs;
the earlier seven-website-unlocks confirmation is a separate owner report.

## Accepted notifications

Both images are genuine in-game captures from the combined review build, resized
to 1280x720 without cropping or changing their contents. The selected popups show
no account name. Game imagery remains the property of its respective rights holders.
They establish RA notification behavior, not complete widescreen compatibility.

![Accepted achievement 1300](retroachievements/accepted-award-1300.png)

[Capture metadata](retroachievements/capture-64338756241.json): fixture=false,
server-confirmed popup 1300, HTTP 200.

![Accepted achievement 186926](retroachievements/accepted-award-186926.png)

[Capture metadata](retroachievements/capture-88953806540.json): fixture=false,
server-confirmed popup 186926, HTTP 200. Two shots of this same award are not counted
as separate unlocks.

## Preferences fix

`save_preferences()` now updates only `[Mods] RetroAchievements`, preserving
unrelated sections/keys/comments, BOM and CRLF. Reads are section-aware too.
Regressions cover missing file/section, duplicate keys, locked files and write
failure, including unrelated filter/widescreen/VSync values. A failed write is
reported and does not silently claim that preferences were saved.

## Lifecycle / credential / network review

- Owner thread alone handles rc_client, callbacks and guest RAM. A worker performs
  network/file work; generation checks discard stale media callbacks and prevent
  a previous account's award response confirming a new account's popup.
- Shutdown marks the client closing, cancels active WinHTTP, joins the worker,
  drains completions while rc_client is still alive, then destroys the client.
  Tests cover blocking requests and award completion after logout/media unload.
- Passwords are not persisted. Tokens use Windows Credential Manager or a
  per-Windows-user DPAPI-encrypted fallback; preferences and logs contain neither.
  Sign out attempts to remove both stores and reports a deletion failure.
  Disabling RA alone intentionally retains the secure session.
- HTTPS only, explicit official-host allowlist and port 443, no redirects,
  ordinary certificate verification, finite 3/4/8/8-second WinHTTP timeouts,
  response limit 4 MiB. No relaxed TLS validation.
- Pending awards retry only while the process/session remains alive; they are not
  durably queued across exit. A popup can precede acceptance; the included
  accepted-response/metadata evidence avoids treating a popup alone as success.
- This is a scoped code/test review, not a security certification or a guarantee
  that every transient library-managed credential copy is erased from memory.

## Vendored source provenance

rcheevos 12.5.0, upstream commit
[`1433173220a7eaede6a9ed7a18e94117be1821e0`](https://github.com/RetroAchievements/rcheevos/tree/1433173220a7eaede6a9ed7a18e94117be1821e0).
65 included upstream files match the upstream **Git blob identities** exactly
(working-tree CRLF is not a source modification). Only `src/rc_client.c` differs:
two guards prevent mastery processing after asynchronous logout/game unload.
[Exact local patch](retroachievements/rcheevos-local.patch).
MIT license and the additional third-party notices remain included. See
`vendor/rcheevos/UPSTREAM.json`; no library version bump or opaque binary added.

## Release tests and shared integration

The branch includes main 1922348, with merged audio#4 and VSync#8. It adds a hosted
Windows Release workflow and a ROM-free account lifecycle contract. Automatic
mock tests cover sign-in failure/retry, secure restore, logout/no restore,
rejected credentials, secure-store failures and cancellation. Separate local
ROM-reading tests additionally verify canonical USA/EU hashes, supported-ROM
gating, award retry/deduplication, owner-thread reads and late responses.
These are explicitly **mock** network tests, not substitutes for the live rows above.

The shared launcher helper matches the reviewed filter#6 version: callbacks
accumulate/deduplicate per target. Six registration orders and target isolation
are tested. The separate combined build uses native/wide source selection ->
optional filter -> aspect-correct presentation -> RA overlay last, and has15
passing contracts plus live native/wide/filter evidence. Independent textual
merges must retain that semantic ordering. PAL and widescreen implementations
are not bundled into this USA-only RA PR; PAL RA remains unavailable.

F9 uses the same full-output readback helper as#6, with initialized borders and
opaque exported alpha. The renderer-state/letterbox regression runs for RA too.
It changes capture diagnostics only, not camera, enemy activation or rendering.

Local final validation:11 registered ROM-free RA contracts,2 additional ROM-reading/mock-client checks, and15 registered combined review contracts passed. The combined review source and binary are separate from this standalone RA branch.
