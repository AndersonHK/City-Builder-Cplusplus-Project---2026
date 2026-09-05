# Window System Design Notes

Use this guide when changing `InGameWindow`, `UiWidgets`, UI XML under `Data/UI`, query-window text, menus, buttons, or renderer UI quads.

## Intent
- In-game windows are presentation objects, not simulation truth.
- Window definitions live in small XML files so layout can change without recompiling gameplay code.
- The first window/menu system should stay narrow: enough for query/debug inspection and XML-backed tool buttons, not a full immediate-mode UI framework.
- Text should accept UTF-8 input at the byte level without crashing, even while the current bitmap glyph set remains intentionally small.

## Current Shape
- `InGameWindow` loads one `<window>` root and a flat list of `<textField>` elements from XML.
- `UiLayout` loads `<menu>` containers with `<button>` children from XML. Menus may be root menus or child menus anchored to another menu or to a specific parent button.
- Root menus support top-left, bottom-left, bottom-right, and center anchors. Child menus resolve from their parent menu/button rectangle and can stack away from the parent or centered on the parent.
- The renderer loads the lot query window from `Data/UI/lot_query.xml` through `BuildDataPath("UI\\lot_query.xml")`.
- The renderer loads city tool, overlay, and nested menu definitions from `Data/UI/city_tools.xml` through `BuildDataPath("UI\\city_tools.xml")`.
- If the file is missing, malformed, or contains no text fields, `InGameWindow::setFallbackDefinition` creates a built-in `lot_query` layout.
- `AppController::printQueryResult` fills `ViewState::queryWindowLines` for lot, road, and empty RCI-lot queries. The renderer copies the first line into `title` and later lines into `line0`, `line1`, and so on.
- Empty text fields are skipped during layout and draw. This lets a query window show only the fields that apply to the queried selection.
- `hugElements="true"` makes the window height shrink to visible content plus margins. Otherwise the declared `height` is used.
- Missing `x`/`y` on a text field opts it into flow layout. Missing `width` makes it use the window content width.
- UI drawing is screen-space and renderer-owned. `BuildWindowQuads` and `RendererBuildUiMenuQuads` emit background, border, button, and text quads into a dynamic `UiQuadInstanceData` buffer.
- The startup/foreground save/load screen is also screen-space and renderer-owned, but it is code-built rather than XML-backed because it must be available before UI XML has loaded. Its progress bar is horizontally centered near three-quarters screen height.
- `Basic.shader` render mode `6` draws UI quads with `vUiColor` through an orthographic screen-space projection.
- Text is drawn as 5x7 bitmap glyph quads scaled by the renderer. The decoder advances through UTF-8 codepoints; supported lowercase ASCII maps to uppercase, and unsupported codepoints fall back to `?`.
- Text clips to the field rectangle. There is no wrapping, measuring pass, caret, input editing, or rich text yet.
- Button hit testing is controller-owned through resolved screen-space rectangles. Button actions are simple strings mapped by `AppController`, keeping UI XML declarative.
- Buttons may declare a named `icon`; the renderer draws supported icons as bitmap quad glyphs and falls back to button text for unknown or absent icons.
- The city date widget adds four icon-only speed buttons through the UI layout: paused, play, fast, and fast-forward.
- The region view has a top-left `Exit to Desktop` menu button. It closes immediately when no active city is assigned, and opens the same save-before-exit dialog as the centered region escape menu when an active cached city exists from the F3 region-view path.
- The centered escape menus, save-before-exit dialog, quit-to-region dialog, and save-before-leaving-city dialog are ordinary hidden UI menus. `AppController` toggles their visibility from the `escape_menu`, `region_escape_menu`, `open_exit_confirm`, `open_quit_region_confirm`, `exit_save_yes`, `exit_save_no`, `quit_region_save_yes`, `quit_region_save_no`, `city_switch_save_yes`, and `city_switch_save_no` actions. The city-switch dialog is only used when the clicked region city would replace a different dirty cached city.
- RCI zone-tool buttons are instantiated at startup from XML-defined zones in `Data/RCI/rci_tools.xml` and inserted into the `rci_tools` child menu under the Tools menu. RCI desirability overlay buttons are instantiated from XML-defined RCI types and inserted into the `rci_desirability_overlays` child menu under the Overlays menu.
- Static overlay buttons live in the bottom-right Overlays menu. The RCI tile overlay colors parcel/zoning tiles and hides RCI buildings so their parcels remain inspectable while active. RCI desirability overlays are type-specific tint overlays generated from RCI type definitions.
- RCI button actions select XML-backed RCI zone tools; RCI type overlay actions select XML-backed desirability overlays. The RCI catalog owns zoning colors, parcel sizing, density curves, demand ids, and desirability labels so menus stay concerned only with presentation and intent.
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
<menu id="rci_tools" parentMenu="side_tools" parentButton="rci_menu" stack="centered" direction="right" visible="false">
</menu>
```

Supported `<menu>` attributes:

- `id`: menu identifier.
- `x`, `y`, `bottom`: screen-space placement values for root menus; `bottom` is used by bottom-left and bottom-right anchors. For child menus, `x`/`y` are offsets from the resolved parent rectangle.
- `width`, `height`: menu bounds. Missing or zero height uses the flowed button height.
- `buttonWidth`, `buttonHeight`: fallback button size.
- `spacing`: space between flowed buttons.
- `anchor`: `topLeft`, `bottomLeft`, `bottomRight`, or `center`.
- `flow`: `down` or `up`.
- `parentMenu` / `parent`: optional parent menu id. When set, this menu is resolved relative to the parent instead of the screen anchor.
- `parentButton`: optional parent button id inside `parentMenu`. When set, this menu is resolved relative to that button instead of the whole parent menu rectangle.
- `stack` / `stackMode`: `away`/`stacked` keeps the menu edge-aligned from the parent; `centered`/`center` centers the child menu across the parent axis.
- `direction` / `stackDirection`: child expansion direction: `up`, `right`, `down`, or `left`.
- `visible`: `true`, `1`, or `yes` keeps the menu active at load.
- `backgroundR/G/B/A`: menu background color.

Supported `<button>` attributes:

- `id`: button identifier.
- `text`: visible button label.
- `textStringId` / `stringId`: locale string id for visible button text. Use this for every visible button label; `text` is only a development fallback.
- `icon`: optional named icon rendered instead of text when supported. Current gameplay icons are `pause`, `play`, `fast`, and `fastForward`.
- `action`: controller action string.
- `x`, `y`, `width`, `height`: optional overrides relative to the menu.
- `colorR/G/B/A`: default button color.
- `activeR/G/B/A`: button color when its action matches the active tool.

## Rules
- Keep window XML data under `Data/UI` so the existing post-build data copy carries it beside the executable.
- Keep UI rendering presentation-only. Simulation and save systems should expose text/data through view state or snapshots, not know about windows.
- Keep button actions as view/controller intent. Gameplay changes still enter `SimulationRuntime` through queued commands.
- Keep visible button labels localized with `textStringId`; icon-only buttons may leave `text` empty.
- Keep physical key bindings out of menu/window XML; use `AppConfig` for hotkeys and keep UI XML about layout plus action names.
- Keep the XML parser tolerant for UI layout files, but do not silently change gameplay asset validation in `AssetLoader`.
- Preserve the fallback query window so debug inspection still works when UI XML is absent during development.
- Add new text fields by id and set them explicitly from controller/view-state code. Do not depend on field order except for the `lineN` helper convention.
- Prefer flow layout plus margins for optional query fields; use explicit coordinates only when a window has fixed panels or columns.
- Draw UI after world geometry, tile overlays, and query route arrows with depth disabled and restored afterward.
- Menus hidden through `UiLayout::setMenuVisible` or `toggleMenu` should not draw buttons or hit-test button actions.
- Keep RCI zoning settings out of menu XML. Menus select tools; the RCI catalog defines zoning color and parcel sizing.
- Keep RCI zone tools and RCI type overlays separate. Zone tools paint land-use zoning; type overlays visualize desirability for the building type that can grow inside one or more zones.
- Root menu toggles should hide their child menus when closed so child buttons do not remain visible or clickable after the parent disappears.
- Avoid adding a full UI framework until there is a second real window or menu pattern that needs shared behavior beyond the current text fields and buttons.

## Checks
- Build `x64 Release` so `Data/UI` is copied beside the executable.
- Query an unzoned empty tile and a lot with `A`; the window should hide for unzoned empty selections and hug only populated fields.
- Query a road tile with active commute routes and confirm the window summarizes morning and evening commuters by mode/layer/direction.
- Query an empty RCI parcel or constructed no-module RCI lot and confirm the window names the RCI type.
- Query houses and factories to confirm optional residents, jobs, complaints, parameters, and module lines appear only when present.
- Temporarily rename `Data/UI/lot_query.xml` in the output folder and confirm the fallback query window still renders.
- Add a long query line and confirm text clips inside its text field rather than spilling outside the window.
- Toggle the bottom-left `Tools` button and confirm the side menu hides, shows, and does not block world clicks while hidden.
- Open the Tools RCI child menu and confirm it expands centered to the right of the RCI button with one generated button per XML-defined zone plus Unzone.
- Open the bottom-right Overlays menu, then the RCI Desire child menu, and confirm it expands centered to the left with one generated button per XML-defined RCI type.
- In region mode with no active city assigned, click the top-left `Exit to Desktop` button and confirm the app closes without prompting. After F3 returns to region with an active cached city, click `Exit to Desktop` and confirm the centered save-before-exit dialog shows `Yes` and `No`.
- Return to region after editing a city, double-click the same city, and confirm no save-before-leaving-city dialog appears. Then double-click another city and confirm the centered save-before-leaving-city dialog can save or discard the cached city before loading the selected city.
- Select each tool button and confirm active-tool highlighting follows bulldoze, road, query, zoning, and unzone tools.
- Click the date widget speed buttons and confirm active highlighting follows paused, play, fast, and fast-forward.
- Press `Esc`, click `Exit to Region`, and confirm the centered quit-to-region dialog shows `Yes` and `No` choices. `Yes` saves and unloads the active city before returning to region mode; `No` discards the active city runtime state and reloads the region city metadata from disk/default.
- Press `Esc`, click `Exit to Desktop`, and confirm the centered save-before-exit dialog shows `Yes` and `No` choices.

## Related Guides
- `docs/design/renderer.md` owns UI draw ordering, shader mode `6`, dynamic UI quad upload, and zoning overlay draw ordering.
- `docs/design/xml-assets.md` owns the distinction between strict gameplay XML and tolerant UI layout XML.
- `docs/design/lots.md` owns the lot query data that currently feeds the first window.
