# RetroAchievements

On Windows, build normally with `YI_ONLINE_RA=ON` (the default). Select the USA ROM in the launcher, enable RetroAchievements, sign in and select Play. The session token is kept in Windows Credential Manager. F6 toggles the achievement overview; F10 and Shift+F10 browse entries. The integration uses Softcore achievements and supports the USA ROM.

After building `yi_ra_ui_test`, run `ctest --test-dir build -R ra_ui_contract --output-on-failure`. This checks the UI, notification rendering and simulated submission transport without an account or ROM. For the client and exact ROM identity checks, run `yi_ra_test.exe <USA ROM> <Europe Rev 1 ROM> <Europe Rev 2 ROM>` and `yi_ra_ui_test.exe popup.bmp <USA ROM> <Europe Rev 0 ROM> <Europe Rev 1 ROM> <Europe Rev 2 ROM>`. These tests use mock HTTP responses.
