# RCI Smart Grid Sandbox Bitmap Tests

These 64x64 BMP files are editable in Paint.
The loader accepts uncompressed indexed, 24-bit, and 32-bit BMPs, so Paint's default BMP save formats are fine as long as the palette colors stay exact.

Each case has:

- `*_input.bmp`: existing map state before the drag.
- `*_expected.bmp`: expected smart-grid result after the drag.

The test executable rebuilds the RCI plan from the input bitmap, renders the calculated result back to the same palette, and compares it to the expected bitmap.
It also checks the generated normal smart-grid result for road-access depth: one-sided RCI blocks must be no deeper than the tool max depth, and road-bounded blocks may be no deeper than twice that max.

## Palette

Use exact solid colors:

- Empty selected/unselected tile: `RGB(255, 255, 255)`
- Residential RCI zoning: `RGB(0, 192, 0)`
- Industrial RCI zoning: `RGB(255, 224, 0)`
- Street/local street tile: `RGB(192, 192, 192)`
- Road tile: `RGB(144, 144, 144)`
- Avenue tile: `RGB(96, 96, 96)`
- Highway tile: `RGB(48, 48, 48)`
- Occupied/blocker tile: `RGB(192, 0, 192)`

Green/yellow pixels inside the drag area are treated as rebuildable existing RCI zoning. Magenta pixels are physical blockers. Street, road, and avenue pixels count as ground-road frontage. Highway pixels block paint but do not count as RCI frontage in this sandbox.

## Cases

Every initial case uses drag bounds `(8, 8)` through `(55, 55)`.

- `normal_res_empty_64`: residential smart grid, no existing roads.
- `normal_res_edge_street_64`: residential smart grid against an existing edge street.
- `normal_res_existing_grid_64`: residential smart grid over an existing street grid.
- `normal_ind_existing_avenue_64`: industrial smart grid with an avenue and existing RCI zoning.
- `normal_res_existing_highway_64`: residential smart grid with highway blockers.
- `shift_res_existing_roads_64`: Shift residential lots-only mode with existing streets/roads.
- `normal_res_existing_rci_and_blocker_64`: residential smart grid over old RCI zoning with a hard occupied blocker.
- `normal_res_offset_grid_snap_64`: residential smart grid near touching offset street corridors.

## Commands

Build:

```powershell
msbuild "City Builder\RciSmartGridSandboxTests.vcxproj" /p:Configuration=Release /p:Platform=x64
```

Run:

```powershell
& "City Builder\x64\Release\RciSmartGridSandboxTests.exe"
```

Regenerate missing input templates and overwrite expected BMPs from the current planner:

```powershell
& "City Builder\x64\Release\RciSmartGridSandboxTests.exe" --bless
```

When a bitmap comparison fails, the actual image is written beside the executable under `RciSmartGridSandboxActuals`.
