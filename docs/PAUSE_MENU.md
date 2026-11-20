# Host pause menu

Esc opens a two-entry overlay: **Fortsetzen** (Continue, selected by default) and **Beenden** (Quit). Esc again resumes. Quit opens a confirmation with **Abbrechen** (Cancel) selected by default. Window-close requests use the same confirmation.

Keyboard: arrows, Enter/Space, Esc. Mouse: hover/click. Controller: D-pad, A to select and B/Start to return. Resume waits for menu action buttons to be released before forwarding gameplay input.

Guest execution and audio pause while the overlay is open. Resume resets host frame pacing so menu time does not produce catch-up frames. The normal orderly exit and existing SRAM persistence remain unchanged. No manual save, settings or save-state action is added.

The SDL overlay restores renderer colour/blend state and does not change logical aspect, filtering, game pixels, camera or enemy activation. It has no widescreen or RetroAchievements dependency.

`pause_menu_contract` uses a synthetic SDL software surface, without a ROM or game execution. It checks safe defaults, repeat rejection, confirmation/cancel, release gating, keyboard/mouse/controller input and renderer-state preservation at native and wide logical sizes.
