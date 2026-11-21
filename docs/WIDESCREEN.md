# Widescreen

Build normally, select a supported USA or Europe Rev 0/1/2 ROM, then enable **16:9 Widescreen** in the launcher. It is off by default. **F8** compares with the original view; **F9** saves local graphics diagnostics. No ROM is included or modified.

The native 256×224 picture and its upper/lower blanking remain intact. Extra scenery is rendered privately at the sides. Cross-section BG3 cover is derived from Map16 data instead of stale offscreen tiles. Bonus games, narrow rooms and unsupported scenes retain native presentation. Camera movement and enemy/object activation are unchanged; consequently objects can still appear at their original activation boundaries.

Active supported autoscrollers move the player's edge limits outward by 43 pixels per side. The original push, speed/subpixel, Baby Mario and crush handling use those displaced limits. The title-specific GSU immediate hook is guarded by ROM/profile and instruction anchors, is inactive in original-view mode, and does not alter ROM bytes or serialized machine state. Unknown edge code retains native autoscroll presentation.

Validation: `ws_contract` and `ws_pal_contract` use synthetic cartridges and require no game ROM. Both are registered with CTest. To check a supplied ROM's layout without executing it, use `ws_contract --layout-only "path/to/ROM.sfc"`. These checks do not replace gameplay testing.

Contributor-reported USA gameplay reaches **2-8 inclusive with no visible issues reported** after the room-edge, moving-column and completed-picture fixes. See [fixes and before/after evidence](evidence/WIDESCREEN_REVIEW.md). Complete-game or all-region compatibility is not claimed.

## Exposed edges, collisions and fallback policy

The scenery renderer does not extend guest spawn/despawn or enemy activation.
An enemy, egg, projectile, particle or interactive sprite only exists outside the
native view if the guest has already generated it. Consequently an exposed side
can show terrain before its objects appear, and objects can disappear at the
original activation boundary. This limitation is intentional and must be checked
in both travel directions; a wider background is not proof of wider object logic.

Normal collision and camera code remain native. There is one explicit existing
gameplay exception: in verified active autoscrollers the player's horizontal edge
limits move outward by 43 pixels per side. This changes the player's reachable
viewport boundary, even though camera motion, enemy activation, terrain collision,
original push/crush handling and ROM bytes are retained. Do not describe the
entire widescreen feature as changing only output. The title-specific SuperFX
immediate hook is guarded by exact instruction anchors, current scene and switch;
F8/native view disables it. Unknown anchors force native autoscroll geometry.

Single-screen terrain components/narrow rooms, unknown room/layout/form/camera
states and unsupported effects use native fallback. Normal terrain uses modes
1/2; the known Mode 7 map UI and bitmap boss arenas have separate paths. Neither
the presence of these paths nor synthetic tests proves every boss, transformation
or Mode 7 scene compatible. Some transitions retain wide geometry while retaining
native content instead of synthesizing unverified terrain; verify that visually.

## Evidence required before broader release

F9 writes a same-frame native/wide pair, final aspect-correct `present.bmp`,
terrain diagnostics and `info.txt` with actual wide/native selection, region rate
and scene. Raw image sizes are 256x224 / 342x224 and use SNES pixel geometry;
the final presented image is the aspect-ratio reference. Keep the matching log.
F8 changes view for live comparison. Capture both sides of a scene transition.

Current evidence and remaining checks are in the
[review matrix and paired gallery](evidence/WIDESCREEN_REVIEW.md): 38 USA and
13 PAL capture groups, 32 active-wide centers RGB-identical, USA progress through
1-8, left autoscroll edge, post-defeat boss arena and representative PAL 1-1.
Right-edge crush is contributor-confirmed in both views. Current-build active
boss deformation, paired Mode7 visuals and broader PAL/transition coverage remain
incomplete. Widescreen stays experimental and off by default.

Main now includes display filters. The shared presenter selects native/wide source,
then applies the optional filter, then performs aspect-correct presentation.
Both launcher panels are registered once. VSync On/Off does not change room
eligibility, source dimensions or region pacing. RA is not included.

When presentation_wide=0, wide.bmp is only a diagnostic buffer and may not be a current valid wide frame. Use info.txt/log plus present.bmp as the authoritative fallback evidence.

Spotlight rooms using level mode 14 share the validated Map16 landscape path. Native lighting windows, color math, room limits and the entire original picture are retained. This is a presentation fix; it does not widen collision, move the camera or change enemy activation. Intro/title and unsupported PPU layouts keep their existing guards.
