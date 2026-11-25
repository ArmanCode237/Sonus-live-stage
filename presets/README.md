# Live Stage Preset Library

- `synth/factory/NNN.json`: factory patch snapshots, generated once by the engine on first startup (256 presets), then loaded from disk.
- `synth/user/presets.json`: custom synth presets; previously `synth_presets.json`, copied here on first startup.
- `organ/factory/NNN.json`: example organ model settings.
- `organ/user/presets.json`: custom organ presets, migrated from previous location.

Do not replace a custom patch file with factory updates. Back up all four folders before upgrades.
