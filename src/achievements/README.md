# Local achievements

Optional Windows host feature, disabled by default in the launcher. Available
with the four recognized ROM variants: USA and Europe Rev0/Rev1/Rev2. European
rule compatibility is experimental; full gameplay validation remains outstanding.

The fixed 63 conditions reference https://retroachievements.org/game/558 . The
MIT rcheevos trigger core is pinned and vendored separately, without its client,
API, hashing or online service code. No account, HTTP, downloads, cloud sync,
leaderboards, rich presence or official RetroAchievements points are involved.

Read-only WRAM/cartridge-RAM checks run once per emulated frame. Game execution,
SRAM, camera, physics and enemy activation are not changed. F6 shows the local
list; F10/Shift+F10 navigate it. Startup/unlock notifications use a compact overlay.

Unlocks are saved immediately to save.achievements.json beside the active SRAM.
The three game save slots are tracked separately, with a ROM/rules identity.
Atomic replacement and an exclusive sidecar lock protect the JSON; malformed or
mismatched files remain untouched. Writes are synchronous on unlock, not per frame.
The original game-save schedule is unchanged. Copy SRAM and JSON together.
Special challenges cannot be inferred from an imported SRAM-only file. Replacing
or deleting a game slot alone does not reset its separately recorded local unlocks.

Build with YI_LOCAL_ACHIEVEMENTS=ON (Windows default) or OFF to omit this feature.
The launcher checkbox remains OFF until selected. No credential store is read.

ROM-free tests cover all rule parsing, Simple Pudding with five versus six eggs,
per-slot isolation, restart/duplicates, corruption/mismatches, write failures,
locking, native-memory preservation, opt-in defaults and queued notifications.
Synthetic checks do not establish full European gameplay compatibility.

Europe Rev2 gameplay testing confirmed the first five-flower achievement, its
in-game notification and persistence in game save slot 2. See
[the verification images](../../docs/evidence/offline-achievements/README.md).
This does not establish full-set compatibility for all European revisions.
