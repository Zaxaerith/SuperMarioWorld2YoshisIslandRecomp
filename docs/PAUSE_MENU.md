# Host pause menu

Esc opens **Continue**, **Restart game**, **Exit level**, and **Quit**. Continue is selected by default. Restart game and Quit require confirmation with Cancel selected first. Alt + F4 and the window close button exit immediately through the normal shutdown/save path; only the menu Quit action asks for confirmation. Restart game warns that progress since the last save will be lost; see [reset behavior](RETURN_TO_TITLE.md).

**Exit level** performs the same action as **Start + Select** in the original game. It is enabled only when the original game permits Start + Select: a cleared level, with native pause available. New levels, maps, dialogue and disabled player states keep it greyed out. It sends released input followed by Start + Select and release, leaving the original game to perform its normal exit. It does not change completion flags or directly change game mode.

Keyboard: arrows, Enter/Space, Esc. Mouse: hover/click. Controller: D-pad, A to select, B/Start to return. Menu action buttons must be released before gameplay input resumes. Guest execution and audio pause while the overlay is open; host pacing restarts on resume. Normal Quit retains its existing SRAM persistence.

The overlay preserves renderer state. Camera, enemy activation, filtering and native Start + Select handling are unchanged. No save-state action is added. F9 remains available and captures the actual menu in the presentation image.

Synthetic tests check navigation, disabled actions, confirmation, input release, renderer-state preservation and the reset/input lifecycle across all four ROM profiles. Review images belong in the [PR #11 discussion](https://github.com/Zaxaerith/SuperMarioWorld2YoshisIslandRecomp/pull/11), not in the source tree.

Player testing confirmed that Restart game restarts in the same window and Exit level works only in completed levels. The final manual-start build was also tested: restarting from a level retained the expected saved slots.
