# Live Stage — Pro Console V30 (React-only)

Single production GUI: React 18 + TypeScript + Vite. The C++17 engine and its audio/MIDI/DSP processing remain independent. The old HTML/JS/CSS UI is **not included** and no longer served. Every deep link (`/`, `/piano`, `/synth`, `/organ`, `/fx`, `/zones`, `/library`, `/settings`) renders the same React application.

## Build (Windows / macOS / Linux with matching dependencies)

```sh
cd webui
npm ci
npm run typecheck
npm test
npm run build       # writes ui/app; official optimized Vite production bundle
```

For development against a running C++ host (port 8080), use `npm run dev` (port 5173, API/WS proxy). From CMake use `cmake --build build --target webui`. A standalone/offline fallback bundler is available as `npm run build:offline` (requires local `node_modules` and TypeScript); it produced the prebuilt `ui/app` assets in this package because the originally bundled dependencies target Windows while the build environment was Linux.

## Architecture

`Shell` → React views → `ParamKnob` / `ParamFader` / `ParamToggle` → `useParam` → `EngineStore` / `RealtimeLink` → `/ws` or HTTP fallback → `StageApp::handleRealtimeCommand` → `engine.applyParamById` → existing C++ DSP.

The single CSS token system is in `src/styles/theme.css`. Functional base rules remain in `components.css` and `views.css`; the new shared Pro Console appearance and responsive layouts are in `pro-console.css`. No second frontend runtime or separate legacy style bundle is published.

## Program bank

Exactly eight programs, displayed **4 columns × 2 rows** on Performance. Number and name are visible inside each button. Recall uses `/api/programs/recall` with a zero-based slot; STORE selects a destination and opens a confirmation/name dialog; INIT resets only the edit buffer. `programs.json` has eight entries, the first eight from the original file. C++ accepts an old 16-program file only to migrate its first eight programs, backing up the complete original to `programs_16slots_backup.json` at startup.

## Validation

- `npm run typecheck` passed.
- `python tests/check_integration.py` checks 8 programs, the React-only static host, the 2×4 layout and the C++-parameter contract.
- `python tools/visual_audit.py` runs the visual and navigation checks in an offline browser harness, using a MemoryRouter **only inside the test**, without changing the production BrowserRouter. All eight views rendered with zero JS page errors; STORE dialog and keyboard toggle were exercised. FX reports 'Esperando al motor' without a C++ process, which is expected.
- A full Windows C++/ASIO/MIDI and audio-latency validation is still required; visual/browser tests cannot certify DSP correctness.

The existing parameter fixtures and mapping tests still live under `src/params/`; update fixtures after making backend parameter changes.

## Old GUI test migration and catalog tooling

Historical tests that referenced the deleted `ui/js/`, `ui/css/` and HTML
partials were archived under `tests/legacy_ui` instead of being treated as
current React regressions. The deterministic synth factory generator now
produces `webui/src/data/synthFactory.ts` directly. See
`GUI_REDISENO_V30.md` for a validation checklist and current limitations.
