# Widescreen

Build normally, select a supported USA or Europe Rev 0/1/2 ROM, then enable **16:9 Widescreen** in the launcher. It is off by default. **F8** compares with the original view; **F9** saves local graphics diagnostics. No ROM is included or modified.

The native 256×224 picture and its upper/lower blanking remain intact. Extra scenery is rendered privately at the sides. Cross-section BG3 cover is derived from Map16 data instead of stale offscreen tiles. Bonus games, narrow rooms and unsupported scenes retain native presentation. Camera movement and enemy/object activation are unchanged; consequently objects can still appear at their original activation boundaries.

Active supported autoscrollers move the player's edge limits outward by 43 pixels per side. The original push, speed/subpixel, Baby Mario and crush handling use those displaced limits. The title-specific GSU immediate hook is guarded by ROM/profile and instruction anchors, is inactive in original-view mode, and does not alter ROM bytes or serialized machine state. Unknown edge code retains native autoscroll presentation.

Validation: `ws_contract` and `ws_pal_contract` use synthetic cartridges and require no game ROM. Both are registered with CTest. To check a supplied ROM's layout without executing it, use `ws_contract --layout-only "path/to/ROM.sfc"`. These checks do not replace gameplay testing.

Reported manual USA testing reached 1-6 without major issues. Remaining levels and the complete game are not claimed to be verified.
