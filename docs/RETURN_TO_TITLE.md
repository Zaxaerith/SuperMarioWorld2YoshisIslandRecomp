# Return to title

Esc opens Continue / Return to title / Quit. Returning to title requires confirmation with Cancel selected first. The confirmation warns that progress since the last save is lost. Keyboard, mouse and controller use the existing menu controls.

On Windows the host prepares a suspended copy of the same executable with the verified current ROM, bypassing the launcher. Only after audio, controllers, optional services, save locks and SDL are closed does the replacement process start. A failed preparation leaves the current game paused and displays an error. Menu activation keys must be released before the replacement accepts gameplay input.

Return to title skips the normal exit SRAM write: the fresh process loads the last persisted save from the same configured location. Normal Quit retains its existing save behavior. This does not create a save-state, alter game progression or change when the original game commits progress. PAL retains its normal language-selection boot flow. Optional integrations start fresh in the replacement process.

Synthetic tests exercise confirmation/cancel, navigation, native/wide rendering, a suspended fixture child and failure handling. Actual ROM boot after restart requires gameplay validation. The current process-launch backend is Windows-only; other platforms report that restart could not be prepared and keep the game open.
