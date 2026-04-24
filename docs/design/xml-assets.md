# XML Asset Import Design Notes

Use this guide when changing `AssetLoader`, data XML files, or lot/module archetype schemas.

## Intent
- XML data should be strict enough to fail early with useful messages.
- Archetypes are small and local; a lightweight parser is acceptable for this milestone.
- Runtime systems should receive validated module and lot definitions.

## Current Shape
- `AssetLoader` reads module XML from `Data/Modules` and lot XML from `Data/Lots`.
- File stems are fallback ids when an explicit `id` attribute is absent.
- Modules define size, effects, and placeholder render values.
- Lots define an anchor, optional render origin, and initial module references.
- Lot validation ensures module references exist and the anchor is occupied.

## Rules
- Keep errors descriptive; asset failures should not become silent defaults.
- Validate cross-file references after loading modules and lots.
- Prefer simple explicit schema additions over implicit behavior.
- Keep XML-backed archetypes separate from live runtime placement state.

## Checks
- Build `x64 Release` so post-build copying preserves data beside the executable.
- Test duplicate ids, missing required tags, and invalid module references when changing schema behavior.
- Confirm data directory assumptions stay documented if project layout changes.
