# Window System Design Notes

Use this guide when changing `InGameWindow`, UI XML under `Data/UI`, query-window text, or renderer UI quads.

## Intent
- In-game windows are presentation objects, not simulation truth.
- Window definitions live in small XML files so layout can change without recompiling gameplay code.
- The first window system should stay narrow: enough for query/debug inspection, not a full immediate-mode UI framework.
- Text should accept UTF-8 input at the byte level without crashing, even while the current bitmap glyph set remains intentionally small.

## Current Shape
- `InGameWindow` loads one `<window>` root and a flat list of `<textField>` elements from XML.
- The renderer loads the lot query window from `Data/UI/lot_query.xml` through `BuildDataPath("UI\\lot_query.xml")`.
- If the file is missing, malformed, or contains no text fields, `InGameWindow::setFallbackDefinition` creates a built-in `lot_query` layout.
- `AppController::printQueryResult` fills `ViewState::queryWindowLines` for lot queries. The renderer copies the first line into `title` and later lines into `line0`, `line1`, and so on.
- Empty text fields are skipped during layout and draw. This lets a query window show only the fields that apply to the queried lot.
- `hugElements="true"` makes the window height shrink to visible content plus margins. Otherwise the declared `height` is used.
- Missing `x`/`y` on a text field opts it into flow layout. Missing `width` makes it use the window content width.
- UI drawing is screen-space and renderer-owned. `BuildWindowQuads` emits background, border, and text quads into a dynamic `UiQuadInstanceData` buffer.
- `Basic.shader` render mode `6` draws UI quads with `vUiColor` through an orthographic screen-space projection.
- Text is drawn as 5x7 bitmap glyph quads scaled by the renderer. The decoder advances through UTF-8 codepoints; supported lowercase ASCII maps to uppercase, and unsupported codepoints fall back to `?`.
- Text clips to the field rectangle. There is no wrapping, measuring pass, caret, input editing, or rich text yet.

## XML Schema
Current window XML is intentionally simple:

```xml
<window id="lot_query" x="24" y="24" width="460" height="232" margin="16" spacing="6" hugElements="true">
  <textField id="title" height="18" />
  <textField id="line0" height="18" />
</window>
```

Supported `<window>` attributes:

- `id`: window identifier.
- `x`, `y`: screen-space top-left position in pixels.
- `width`, `height`: declared window size in pixels.
- `margin`: fallback margin for all sides.
- `marginLeft`, `marginTop`, `marginRight`, `marginBottom`: side-specific margin overrides.
- `spacing`: vertical spacing between flowed visible text fields.
- `hugElements`: `true`, `1`, or `yes` enables content-hugging height.

Supported `<textField>` attributes:

- `id`: text field identifier used by code.
- `x`, `y`: explicit position inside the window. Both must be present to opt out of flow layout.
- `width`: explicit field width. Missing width uses the window content width.
- `height`: field height in pixels.

## Rules
- Keep window XML data under `Data/UI` so the existing post-build data copy carries it beside the executable.
- Keep UI rendering presentation-only. Simulation and save systems should expose text/data through view state or snapshots, not know about windows.
- Keep the XML parser tolerant for UI layout files, but do not silently change gameplay asset validation in `AssetLoader`.
- Preserve the fallback query window so debug inspection still works when UI XML is absent during development.
- Add new text fields by id and set them explicitly from controller/view-state code. Do not depend on field order except for the `lineN` helper convention.
- Prefer flow layout plus margins for optional query fields; use explicit coordinates only when a window has fixed panels or columns.
- Draw UI after world geometry, tile overlays, and query route arrows with depth disabled and restored afterward.
- Avoid adding a full UI framework until there is a second real window that needs shared behavior beyond text fields.

## Checks
- Build `x64 Release` so `Data/UI` is copied beside the executable.
- Query an empty tile and a lot with `A`; the window should hide for empty selections and hug only populated lot fields.
- Query houses and factories to confirm optional residents, jobs, complaints, parameters, and module lines appear only when present.
- Temporarily rename `Data/UI/lot_query.xml` in the output folder and confirm the fallback query window still renders.
- Add a long query line and confirm text clips inside its text field rather than spilling outside the window.

## Related Guides
- `docs/design/renderer.md` owns UI draw ordering, shader mode `6`, and dynamic UI quad upload.
- `docs/design/xml-assets.md` owns the distinction between strict gameplay XML and tolerant UI layout XML.
- `docs/design/lots.md` owns the lot query data that currently feeds the first window.
