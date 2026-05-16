# App Config Design Notes

Use this guide when changing `AppConfig`, `Data/config.ini`, startup window behavior, configurable hotkeys, date display settings, or debug console output.

## Intent
- Keep local presentation and input preferences data-driven without making them part of city or region saves.
- Let missing or partially invalid config files fall back to safe compiled defaults so the game can still boot.
- Keep simulation truth out of config. Config may change how values are displayed or which key triggers a command, but not what the saved city state means.

## Current Shape
- `Source.cpp` creates one `AppConfig` during startup, loads `Data/config.ini` from the executable directory, then passes that read-only object to `AppController` and `Renderer`.
- `Data/config.ini` is copied beside the executable by the project post-build data copy. It is intentionally not loaded from the source tree at runtime.
- `[window]` controls startup mode. `fullscreen=true` opens directly on the primary monitor; `windowed_width` and `windowed_height` are the preferred size used for windowed startup and for restoring from fullscreen.
- `Alt+Enter` remains handled by `RendererCallbacks`, before `AppController` sees keyboard input, because it mutates GLFW window/monitor state rather than game tool state.
- `[hotkeys]` maps action names to GLFW-compatible key codes or names. `AppController::onKeyPressed` receives raw GLFW key values from the renderer, so config parsing must produce the same key-code space.
- `[date]` controls display formatting only. The actual date belongs to the active city through its saved simulation tick; regions do not own date state.
- `[debug] print_query_values_to_console=false` gates the verbose query-value dump in `AppController::printQueryResult`. It does not silence general status logs such as selected tool, road template, zoom, or renderer metrics.

## Rules
- Prefer adding new app-level preferences here instead of hardcoding new startup/input/debug behavior in `Source.cpp`, `Renderer`, or `AppController`.
- Keep config tolerant: unknown sections, unknown keys, and invalid values should preserve existing defaults rather than aborting startup.
- Keep config read-once until there is an explicit live-reload design. Runtime systems currently hold references to the startup `AppConfig`.
- When adding a hotkey, update `HotkeyConfig`, `ApplyHotkey`, `Data/config.ini`, README controls, and any relevant design checks together.
- Do not store city, region, or save compatibility state in this INI file.
- Do not delete or recreate `Data\Saves` as part of config loading, post-build copying, or startup fallback.

## Checks
- Build `x64 Release` and confirm `Data/config.ini` is copied beside the executable.
- Remove or rename the output `Data/config.ini` and confirm compiled defaults still boot.
- Set `fullscreen=false` and confirm startup uses the configured windowed dimensions.
- Change one tool hotkey and confirm the old key no longer selects it while the new key does.
- Set each supported date format: `YYYY/MM/DD`, `MM/DD/YYYY`, and `DD/MM/YYYY`.
- Toggle `print_query_values_to_console` and confirm query windows still update in both modes while console query dumps only print when enabled.

## Related Guides
- `docs/design/renderer.md` owns GLFW window creation, fullscreen toggling, HUD drawing, and renderer-owned UI draw order.
- `docs/design/window-system.md` owns XML-backed in-game UI layout; config only decides app preferences and hotkeys.
- `docs/design/region-save.md` owns city/region save state and the rule that existing saves are not deleted.
