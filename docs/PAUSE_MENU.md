# Host pause menu

On Windows, Esc opens **Continue** (selected by default), **Return to title**, and **Quit**. Other platforms offer **Continue** and **Quit**. Esc again resumes. Both Return to title and Quit require confirmation, with **Cancel** selected by default. Window-close requests use the Quit confirmation. Return to title warns that progress since the last save is lost.

Return to title restarts the same ROM on Windows, bypassing the launcher and loading the last persisted game save without an additional exit save write. Preparation failures keep the current game open. See [restart behavior](RETURN_TO_TITLE.md).

Keyboard: arrows, Enter/Space, Esc. Mouse: hover/click. Controller: D-pad, A to select and B/Start to return. Resume waits for menu action buttons to be released before forwarding gameplay input.

Guest execution and audio pause while the overlay is open. Resume resets host frame pacing so menu time does not produce catch-up frames. Normal Quit retains its existing SRAM persistence. No manual save, settings or save-state action is added.

The SDL overlay restores renderer colour/blend state and does not change logical aspect, filtering, game pixels, camera or enemy activation. It has no widescreen or RetroAchievements dependency.

`pause_menu_contract` uses a synthetic SDL software surface, without a ROM or game execution. It checks safe defaults, repeat rejection, confirmation/cancel, release gating, keyboard/mouse/controller input and renderer-state preservation at native and wide logical sizes.

## Platform support and verification

Return to title is offered only on Windows, where the current backend restarts the same executable and verified ROM. Other platforms retain Continue/Quit and never offer the unsupported action. This is a process restart, not an in-process game reset.

The portable menu path is also exercised by a ROM-free unsupported-backend contract. Actual Windows title restart and persisted save reload still require a short real-game acceptance check; synthetic process tests do not prove that flow.

F9 remains available while paused and captures the actual menu in the presentation image.

Review images and gameplay results belong in the [PR #11 discussion](https://github.com/Zaxaerith/SuperMarioWorld2YoshisIslandRecomp/pull/11), not in the source tree.
