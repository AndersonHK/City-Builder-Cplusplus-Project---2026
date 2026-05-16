# Window System Design Notes

Use this guide when changing `InGameWindow`, `UiWidgets`, UI XML under `Data/UI`, query-window text, menus, buttons, or renderer UI quads.

## Intent
- In-game windows are presentation objects, not simulation truth.
- Window definitions live in small XML files so layout can change without recompiling gameplay code.
- The first window/menu system should stay narrow: enough for query/debug inspection and XML-backed tool buttons, not a full immediate-mode UI framework.
- Text should accept UTF-8 input at the byte level without crashing, even while the current bitmap glyph set remains intentionally small.

## Current Shape
- `InGameWindow` loads one `<window>` root and a flat list of `<textField>` elements from XML.
- `UiLayout` loads flat `<menu>` containers with `<button>` children from XML. It falls back to a built-in city tool menu when XML is absent.
- The renderer loads the lot query window from `Data/UI/lot_query.xml` through `BuildDataPath("UI\\lot_query.xml")`.
- The renderer loads the city tool buttons from `Data/UI/city_tools.xml` through `BuildDataPath("UI\\city_tools.xml")`.
- If the file is missing, malformed, or contains no text fields, `InGameWindow::setFallbackDefinition` creates a built-in `lot_query` layout.
- `AppController::printQueryResult` fills `ViewState::queryWindowLines` for lot, road, and empty RCI-lot queries. The renderer copies the first line into `title` and later lines into `line0`, `line1`, and so on.
- Empty text fields are skipped during layout and draw. This lets a query window show only the fields that apply to the queried selection.
- `hugElements="true"` makes the window height shrink to visible content plus margins. Otherwise the declared `height` is used.
- Missing `x`/`y` on a text field opts it into flow layout. Missing `width` makes it use the window content width.
- UI drawing is screen-space and renderer-owned. `BuildWindowQuads` and `RendererBuildUiMenuQuads` emit background, border, button, and text quads into a dynamic `UiQuadInstanceData` buffer.
- `Basic.shader` render mode `6` draws UI quads with `vUiColor` through an orthographic screen-space projection.
- Text is drawn as 5x7 bitmap glyph quads scaled by the renderer. The decoder advances through UTF-8 codepoints; supported lowercase ASCII maps to uppercase, and unsupported codepoints fall back to `?`.
- Text clips to the field rectangle. There is no wrapping, measuring pass, caret, input editing, or rich text yet.
- Button hit testing is controller-owned through resolved screen-space rectangles. Button actions are simple strings mapped by `AppController`, keeping UI XML declarative.
- RCI button actions select XML-backed RCI tools; the tool details live in `Data/RCI/rci_tools.xml` so menus stay concerned only with presentation and intent.
- Keyboard shortcuts are not stored in UI XML. They live in `Data/config.ini` and are dispatched through `AppController`, so menu button actions and hotkeys can point at the same tool intent without coupling the UI schema to physical keys.

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

Current menu XML is also intentionally simple:

```xml
<menu id="side_tools" anchor="bottomLeft" x="16" bottom="72" width="132" buttonWidth="132" buttonHeight="38" spacing="8">
  <button id="bulldozer" text="Bulldoze" action="select_bulldozer" />
</menu>
```

Supported `<menu>` attributes:

- `id`: menu identifier.
- `x`, `y`, `bottom`: screen-space placement values. `bottom` is used by `anchor="bottomLeft"`.
- `width`, `height`: menu bounds. Missing or zero height uses the flowed button height.
- `buttonWidth`, `buttonHeight`: fallback button size.
- `spacing`: space between flowed buttons.
- `anchor`: `topLeft` or `bottomLeft`.
- `flow`: `down` or `up`.
- `visible`: `true`, `1`, or `yes` keeps the menu active at load.
- `backgroundR/G/B/A`: menu background color.

Supported `<button>` attributes:

- `id`: button identifier.
- `text`: visible button label.
- `action`: controller action string.
- `x`, `y`, `width`, `height`: optional overrides relative to the menu.
- `colorR/G/B/A`: default button color.
- `activeR/G/B/A`: button color when its action matches the active tool.

## Rules
- Keep window XML data under `Data/UI` so the existing post-build data copy carries it beside the executable.
- Keep UI rendering presentation-only. Simulation and save systems should expose text/data through view state or snapshots, not know about windows.
- Keep button actions as view/controller intent. Gameplay changes still enter `SimulationRuntime` through queued commands.
- Keep physical key bindings out of menu/window XML; use `AppConfig` for hotkeys and keep UI XML about layout plus action names.
- Keep the XML parser tolerant for UI layout files, but do not silently change gameplay asset validation in `AssetLoader`.
- Preserve the fallback query window so debug inspection still works when UI XML is absent during development.
- Add new text fields by id and set them explicitly from controller/view-state code. Do not depend on field order except for the `lineN` helper convention.
- Prefer flow layout plus margins for optional query fields; use explicit coordinates only when a window has fixed panels or columns.
- Draw UI after world geometry, tile overlays, and query route arrows with depth disabled and restored afterward.
- Menus hidden through `UiLayout::setMenuVisible` or `toggleMenu` should not draw buttons or hit-test button actions.
- Keep RCI zoning settings out of menu XML. Menus select tools; the RCI catalog defines zoning color and parcel sizing.
- Avoid adding a full UI framework until there is a second real window or menu pattern that needs shared behavior beyond the current text fields and buttons.

## Checks
- Build `x64 Release` so `Data/UI` is copied beside the executable.
- Query an unzoned empty tile and a lot with `A`; the window should hide for unzoned empty selections and hug only populated fields.
- Query a road tile with active commute routes and confirm the window summarizes commuters by mode/layer/direction.
- Query an empty RCI parcel or constructed no-module RCI lot and confirm the window names the RCI type.
- Query houses and factories to confirm optional residents, jobs, complaints, parameters, and module lines appear only when present.
- Temporarily rename `Data/UI/lot_query.xml` in the output folder and confirm the fallback query window still renders.
- Add a long query line and confirm text clips inside its text field rather than spilling outside the window.
- Toggle the bottom-left `Tools` button and confirm the side menu hides, shows, and does not block world clicks while hidden.
- Select each tool button and confirm active-tool highlighting follows bulldoze, road, query, and zoning tools.

## Related Guides
- `docs/design/renderer.md` owns UI draw ordering, shader mode `6`, dynamic UI quad upload, and zoning overlay draw ordering.
- `docs/design/xml-assets.md` owns the distinction between strict gameplay XML and tolerant UI layout XML.
- `docs/design/lots.md` owns the lot query data that currently feeds the first window.
