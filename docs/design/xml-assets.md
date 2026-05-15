# XML Asset Import Design Notes

Use this guide when changing `AssetLoader`, data XML files, lot/module archetype schemas, or the split between gameplay XML and UI layout XML.

## Intent
- XML data should be strict enough to fail early with useful messages.
- Archetypes are small and local; a lightweight parser is acceptable for this milestone.
- Runtime systems should receive validated module and lot definitions.

## Current Shape
- `AssetLoader` reads module XML from `Data/Modules`, lot XML from `Data/Lots`, and the congestion curve from `Data/TransportNetwork/congestion.xml` when present.
- UI window XML lives under `Data/UI` and is parsed by `InGameWindow`, not `AssetLoader`.
- File stems are fallback ids when an explicit `id` attribute is absent.
- Modules define size, effects, and placeholder render values.
- Modules may also define city-parameter contributions inside `<parameters>` using `<driver>` or `<satisfaction>` tags.
- Lots define an anchor, optional explicit footprint, optional render origin, optional front direction, initial module references, and optional access connections.
- Lot validation ensures module references exist, the anchor is inside the lot footprint, access tiles are inside the footprint, access directions point outside the footprint, and access modes are known.
- The congestion XML defines `<point utilization="..." speedMultiplier="..." />` rows. Invalid or duplicate utilization points fail asset load.
- The query window XML defines one `<window>` plus flat `<textField>` entries. It is tolerant and falls back to a built-in layout when missing or malformed.

## Rules
- Keep errors descriptive; asset failures should not become silent defaults.
- Validate cross-file references after loading modules and lots.
- Validate parameter ids against `CityParameterRegistry` during asset load.
- Validate explicit footprints: positive dimensions, anchor inside footprint, and initial modules fully inside the footprint.
- Validate access declarations before normalizing the lot anchor, then store them relative to the normalized anchor so placement rotation can transform them.
- Prefer simple explicit schema additions over implicit behavior.
- Keep XML-backed archetypes separate from live runtime placement state.
- Keep UI layout XML separate from gameplay archetype XML. UI parser fallbacks are acceptable for tools/debug windows, but gameplay asset XML should continue to fail early.

## Checks
- Build `x64 Release` so post-build copying preserves data beside the executable.
- Test duplicate ids, missing required tags, and invalid module references when changing schema behavior.
- Confirm data directory assumptions stay documented if project layout changes.
- Confirm new lot/module XML files are copied beside the executable by the project post-build step.
- Confirm new UI XML files under `Data/UI` are copied beside the executable by the same post-build data copy.

## Related Guides
- `docs/design/window-system.md` owns the current window XML attributes and query-window layout behavior.
