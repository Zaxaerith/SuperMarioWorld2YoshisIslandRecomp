# VSync

The launcher graphics settings expose an On/Off VSync control. It defaults to Off and is saved as `Graphics/VSync=0` or `1` in `config.ini`. The selected value is applied when the game renderer is created; restart the game to change it. Game timing remains region-paced.

Build normally and run `YoshisIslandRecomp --vsync-selftest` for the ROM-free default/persistence test. For a display check, switch VSync in the launcher and inspect the `[Video]` renderer log. Window composition and driver overrides can still affect displayed timing; this setting controls the application's SDL request.
