# XML Asset Import Design Notes

Use this guide when changing `AssetLoader`, data XML files, lot/module archetype schemas, or the split between gameplay XML and UI layout XML.

## Intent
- XML data should be strict enough to fail early with useful messages.
- Archetypes are small and local; a lightweight parser is acceptable for this milestone.
- Runtime systems should receive validated module and lot definitions.

## Current Shape
- `AssetLoader` reads module XML from `Data/Modules`, lot XML from `Data/Lots`, initial RCI demand XML from `Data/RCI/initial_demands.xml` when present, the congestion curve from `Data/TransportNetwork/congestion.xml` when present, and road lane capacities from `Data/TransportNetwork/lane_capacities.xml` when present.
- UI window/menu XML lives under `Data/UI` and is parsed by `InGameWindow` and `UiLayout`, not `AssetLoader`.
- RCI XML lives under `Data/RCI` and is parsed by both `RciToolCatalog` and `AssetLoader`. Zone entries define zoning-tool id/name/localized label/color plus min/preferred/max lot depth and width. RCI type entries define demand/desirability identity and the zone types they can grow inside. This is intentionally not one-to-one: multiple zones can grow the same RCI type, and one zone can later allow multiple RCI types.
- `AssetLoader` also reads constructor knobs, the tile land-value baseline, and zone-owned density curves from the same root: `constructorAttemptsPerTick`, `constructorOverbuildPercent`, `baselineLandValue`, and `<zone desirabilityThreshold="...">` entries with `maxDensityPerTile` rows. Legacy `<tool>` and `<rciGrowth>` entries remain compatibility inputs.
- App preferences are intentionally not XML-backed. Startup window options, hotkeys, date display format, and query debug output live in `Data/config.ini` and are parsed by `AppConfig`.
- File stems are fallback ids when an explicit `id` attribute is absent.
- Modules define size, effects, placeholder render values, and optional `density` metadata. Primary RCI modules self-categorize as `low`, `medium`, or `high`; secondary yard, path, parking, and service modules leave it empty.
- Modules may also define city-parameter contributions inside `<parameters>` using `<driver>` or `<satisfaction>` tags.
- Lots define an anchor, optional constructor-facing zone `zoningType`, optional RCI type id `rciType`, required RCI `name` and `densityBand`, optional `constructionDays`, legacy optional `constructionTicks`, optional explicit footprint, optional render origin, optional front direction, initial module references, and optional access connections.
- Lot access can be declared with individual `<connection>` rows or a compact `<perimeter modes="..." />` row inside `<access>`, which expands to all exterior footprint edges after the footprint has been declared. Residential RCI lots should use explicit front-edge connections matching their driveway, path, garden, or parking entrance tiles instead of perimeter access.
- Lot validation ensures module references exist, the anchor is inside the lot footprint, access tiles are inside the footprint, access directions point outside the footprint, and access modes are known.
- The congestion XML defines `<point utilization="..." speedMultiplier="..." />` rows. Invalid or duplicate utilization points fail asset load.
- The road lane capacity XML defines `<lane type="slow|medium|fast|pedestrian" capacity="..." />` rows. All four lane types must be present, and capacities must be positive.
- Initial demand XML defines `<demand id="..." amount="..." />` rows keyed by city parameter id. The checked-in defaults seed 20 low-wealth residents and 20 dirty-industry demand.
- RCI constructor overbuild is stored as a multiplier internally. XML may spell `constructorOverbuildPercent="120"` for 120 percent, or `constructorOverbuildMultiplier="1.2"` for the same result.
- `baselineLandValue` in `Data/RCI/rci_tools.xml` seeds new tiles and is currently `0`. Imported land values are clamped to the displayed land-value min/cap range. Starter RCI growth is preserved by a constructor density floor instead of by pretending empty land has high land value.
- Each constructor-enabled zone must have a matching density curve. The preferred form is `<zone zoningType="..." desirabilityThreshold="...">` with one or more `<maxDensityPerTile population="..." value="..." />` rows, sorted and linearly interpolated at runtime by current city population. The density value is a hard lot-capacity ceiling: `capacity <= maxDensityPerTile * footprintArea`.
- The checked-in low-density residential zone requires desirability 60 and flattens at max density 1.75 after population 3,000. The checked-in high-density residential zone inherits the full residential curve and reaches max density 8 at population 1,000,000. The checked-in industrial zone requires desirability 60 and reaches max density 3 at population 1,000,000.
- The query window XML defines one `<window>` plus flat `<textField>` entries. City tool XML defines root and child `<menu>` containers plus `<button>` children, action strings, optional named icons, top-left/bottom-left/bottom-right/center anchors, parent menu/button ids, and child-menu stack mode/direction. Both UI parsers are tolerant and fall back to built-in layouts when missing or malformed.
- `Data/Locale/*.json` supplies UTF-8 localized strings by id. Current RCI zone tool labels use zone `labelStringId`, RCI desirability overlay labels use type `desirabilityOverlayStringId`, and startup fails with a descriptive error if a referenced string id is missing.

## Rules
- Keep errors descriptive; asset failures should not become silent defaults.
- Validate cross-file references after loading modules and lots.
- Validate parameter ids against `CityParameterRegistry` during asset load.
- Validate module `density` values when present.
- Validate explicit footprints: positive dimensions, anchor inside footprint, and initial modules fully inside the footprint.
- Validate access declarations before normalizing the lot anchor, then store them relative to the normalized anchor so placement rotation can transform them.
- Validate lot zoning type names when present; unknown `zoningType` / `rciType` values should fail asset load.
- Validate RCI zone growth rules for constructor-enabled zoning types. Missing rules, duplicate zoning entries, invalid thresholds, duplicate/non-increasing populations, and non-positive density values should fail asset load.
- Validate RCI zone/type relationships separately: zones own zoning color, parcel sizing, and density curves; RCI types own demand ids, desirability overlays, and their allowed zone list.
- Validate RCI lot metadata. Constructor-enabled lots require a display `name` and a `densityBand`, and their primary module metadata should match that density band.
- Prefer `constructionDays` for authored lot construction duration. `AssetLoader` converts logical days to stored runtime ticks with `SimulationTime::daysToTicks()` at load time. Legacy `constructionTicks` remains supported as raw ticks for compatibility. A zero duration is immediately active; positive values render construction growth and delay city-parameter effects.
- Keep runtime fields such as construction remaining ticks and save-state tick counters in ticks because they represent elapsed simulation state, not authored logical duration.
- Prefer simple explicit schema additions over implicit behavior.
- Keep XML-backed archetypes separate from live runtime placement state.
- Keep UI layout XML separate from gameplay archetype XML. UI parser fallbacks are acceptable for tools/debug windows, tool buttons, and the first RCI tool catalog; gameplay lot/module asset XML should continue to fail early.
- Keep app preferences out of XML asset loaders. `AppConfig` is tolerant and startup-focused; gameplay XML should remain strict and validated.

## Checks
- Build `x64 Release` so post-build copying preserves data beside the executable.
- Test duplicate ids, missing required tags, and invalid module references when changing schema behavior.
- Confirm data directory assumptions stay documented if project layout changes.
- Confirm new lot/module XML files are copied beside the executable by the project post-build step.
- Confirm new UI XML files under `Data/UI` are copied beside the executable by the same post-build data copy.
- Confirm new RCI XML files under `Data/RCI` are copied beside the executable by the same post-build data copy.

## Related Guides
- `docs/design/window-system.md` owns the current window XML attributes and query-window layout behavior.
