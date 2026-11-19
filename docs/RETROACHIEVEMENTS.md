# RetroAchievements

On Windows, build normally with `YI_ONLINE_RA=ON` (the default). Select the USA ROM in the launcher, enable RetroAchievements, sign in and select Play. The session token is kept in Windows Credential Manager. F6 toggles the achievement overview; F10 and Shift+F10 browse entries. The integration uses Softcore achievements and supports the USA ROM.

If Credential Manager rejects a write, the session uses a per-Windows-user DPAPI-
encrypted file under `%LOCALAPPDATA%/YoshisIslandRecomp`. The password is not saved.
Sign out removes both stores; a removal failure is reported instead of claiming
that the saved session was deleted. Disabling RA is not the same as signing out.
The RA toggle preserves unrelated keys, sections, comments and line endings in
the preferences file, including Widescreen, filter and VSync entries.

## Real-account evidence and limits

On 19 November 2026 the contributor reported **seven legitimate achievement
unlocks visible on the RetroAchievements website**, from their own real-account
gameplay testing. USA Softcore session logs also contain accepted award responses
for game 558. This is owner-reported live evidence, not an automated account
audit; no credentials or account exports are included. It must not be described
as a mock-only validation. The seven achievements need not be earned again.

The exact updated integration binary has not yet completed a new live lifecycle
test. Still record: login/notification, token restore after normal process exit,
sign out followed by restart without automatic login, and an interrupted
connection followed by retry while the same session remains open. Pending awards
are not durably queued across process exit. A popup can precede server acceptance;
verify the account website or an accepted response, not just the popup counter.

## Lifecycle and dependency review

`vendor/rcheevos/UPSTREAM.json` pins MIT rcheevos 12.5.0, commit
`1433173220a7eaede6a9ed7a18e94117be1821e0`. The only changed upstream file is
`src/rc_client.c`: two guards protect mastery processing after game unload/logout.
The included 65 other upstream files match the pinned Git blobs. License notices
and the local modification manifest are included beside the library.

HTTP is worker-only; completions and RAM reads run on the owner thread. Shutdown
marks the client closing, cancels active HTTP, joins the worker, drains callbacks
before destroying rc_client, and prevents further outgoing requests. WinHTTP
accepts only the listed official HTTPS hosts on port 443, refuses redirects,
uses normal certificate verification, limits responses to 4 MiB and uses finite
timeouts. This is a scoped source/test review, not a formal security certification.

`ra_preferences_contract` checks unrelated INI data, BOM/CRLF, missing keys/files,
duplicate keys, locked files and write failures. `ra_lifecycle_test <USA ROM>`
uses mock HTTP to exercise restored-token login, already-earned deduplication,
offline retry/reconnect, logout, storage-deletion error reporting, owner-thread
memory reads and cancellation while HTTP is blocked. It hashes the supplied ROM
read-only; it does not execute the game or submit actual achievements.

The local Windows CI workflow builds Release and runs ROM-free CTest targets.
Preparing the workflow does not mean that a hosted CI run has passed. The live
account steps above and full integration with filters/widescreen require separate
evidence. PAL online awards remain outside this PR's supported scope.

After building `yi_ra_ui_test`, run `ctest --test-dir build -R ra_ui_contract --output-on-failure`. This checks the UI, notification rendering and simulated submission transport without an account or ROM. For the client and exact ROM identity checks, run `yi_ra_test.exe <USA ROM> <Europe Rev 1 ROM> <Europe Rev 2 ROM>` and `yi_ra_ui_test.exe popup.bmp <USA ROM> <Europe Rev 0 ROM> <Europe Rev 1 ROM> <Europe Rev 2 ROM>`. These tests use mock HTTP responses.
