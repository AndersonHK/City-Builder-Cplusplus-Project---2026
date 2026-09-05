# Visual asset pipeline

Implemented revision: 2026-09-05. The physical contract is [6 m per tile](metric-art-standard.md).

## Authored data and cooking

`City Builder/Data/Modules/*.xml` contains stable module IDs, simulation stats,
and metric render recipes. The catalog has 80 modules: the existing 75, three composable driveway tiles, and two
legacy rowhouse IDs retained for save compatibility. `Data/Lots` contains the existing lot layouts;
15 are active and the low-court template remains intentionally disabled.

The render recipe declares `family`, `widthMeters`, `depthMeters`,
`heightMeters`, `floors`, `seed`, wall `colorR/G/B`, `metric=true` and `stretch`.
Use `stretch=false` for buildings, trees and vehicle-bearing pieces. Only
plain ground surfaces can stretch. Mesh keys are `metric_<module ID>`.
Floors and height must be edited together so generated geometry fits its
physical envelope. Cooking rejects unknown families, invalid dimensions and
vertices outside the declared bounds.

`AssetGenerator.cpp` calls `AssetPipeline.h` and `AssetMeshBuilder.h` to cook:

- `Generated/module_meshes.txt`: CBGM version 2 static triangle catalog, with
  position, vertex color, outward normal, metre UV, material layer and AO.
- `Generated/asset_report.csv`: family, dimensions, floors, capacity, and near/far triangle counts.
- `Generated/materials.ppm`: inspectable swatches for the 12 original materials.

The main families include detached houses and duplexes with porches, repeated
rowhouse units, walkups, courtyard wings, apartment blocks, towers on podiums,
warehouses with loading doors, factory rooflights, cylindrical stacks, trees,
benches, industrial clutter, and planted ground. Windows have recesses, sills
and mullions; facades have entrances, roof trim and floor divisions.

`LotMaterials.h` supplies the same original brick, roof, render, concrete,
asphalt, grass, foliage, metal, wood, glass and gravel swatches to the cooker,
game and editor. `LotMaterialShader.h` supplies their shared directional and
ambient shading. Material UVs use metre units. Texture arrays are mipmapped.
This is a simple textured renderer, not a full PBR or glTF import pipeline.
The former glTF proposal remains a possible future interoperability step.

## Placement and old saves

`AssetLoader` reads dimensions in metres. `Lot::buildRenderInstances` restores
those physical dimensions and rotates meshes as well as footprints. Saved
primitive-era visual stretches no longer deform rigid metric models.
Simulation footprints, IDs, occupancy and save serialization stay compatible.
Decorative props inherit the parent orientation without affecting capacity.

`LotAccessVisuals.h` reconstructs entrance paths and type-appropriate vehicle
access using physical building bounds. Long private drives are built from
1x1 middle pieces and left/right path caps; cars are never stretched with
length. Old short path and driveway instances are replaced in the presentation
snapshot. Trees and planted beds are cleared where new access runs.

## Runtime cost

Each module also cooks a `_distant` mesh with the same dimensions and silhouette.
The game switches to those meshes above 64 visible tiles and uses them for
region thumbnails. High-detail facades retain physical window/trim geometry;
distant facades use simple window planes. The reviewed catalog totals 440,212
near triangles versus 52,976 distant triangles (88% fewer). Draws remain
instanced and batched by mesh. Large-save frame-time benchmarking remains a
separate useful check; triangle reduction alone is not a frame-rate guarantee.

## Native Asset Manager

Build `City Builder/AssetManager.vcxproj` in Release/x64 and launch
`Distributable/x64/Release/AssetManager.exe`. From this repository it edits the
source `City Builder/Data`; when copied with the game it edits adjacent Data.
`--data <directory>` selects another data tree explicitly.

- Choose Complete lots or Modules and filter by ID.
- Set parcel width/depth in tiles, rotate, choose a variation, drag to orbit,
  and wheel to zoom. Lots use the production parcel fitter and render instances.
- Inspect stats, capacity, physical dimensions and triangle count. Toggle scale
  props, wireframe or distant LOD. The reference street is 12 m wide.
- In a lot, select a module from “Edit module in lot” to edit metric dimensions,
  floors, capacity, palette and effects with the fields. Save fields validates
  and regenerates; advanced Save XML also edits layout rules and metadata.
- Saving stages the whole data tree, validates references and geometry, keeps
  an XML `.bak`, and replaces the source and catalog. Concurrent source changes
  and ID changes are rejected. Module edits affect every lot using that module.
- Deploy to game publishes saved definitions and cooked art next to the game.
  Restart the game to load them. Save/load of cities remains in the game.
- Export BMP captures the current OpenGL viewport. `--capture <directory>`
  exports every module, both lot orientations, compact parcels, a distant tower,
  and the manager UI. It compiles the actual game shader during initialization.

The manager adds a reference road and planar contact shadows for inspection.
The game shares meshes, material shading and placement, but its world context
and shadow presentation differ. Editor captures are not game screenshots.

## Build and verification

From the repository root (MSVC v143 / Windows SDK installed):

```powershell
python tools/msbuild.py 'City Builder/City Builder.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
python tools/msbuild.py 'City Builder/AssetManager.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& 'Distributable/x64/Release/AssetManager.exe' --validate
& 'Distributable/x64/Release/AssetManager.exe' --self-test 'Build/ArtReview'
& 'Distributable/x64/Release/AssetManager.exe' --capture 'Build/ArtReview'
```

The wrapper normalizes Windows environment key casing before invoking MSBuild.
Both projects run `tools/deploy-assets.ps1`: copy authored data, retire stale
module/lot XML absent from source, cook assets, and propagate cooker failures.
Generated catalogs, review captures and binaries are excluded from version control.

Validation exercises both detail levels and all accepted parcel sizes/rotations.
The editor self-test saves into an isolated fixture and checks backups, invalid
edit isolation, conflict detection and stable IDs. RCI tests cover the connected
sidewalk/door/cap graph, integral driveway pieces and the rowhouse exception.
Run RendererTests and SaveLoadIntegrationTests alongside RciLotConstructionTests
when changing the catalog format, instance transforms or snapshot behavior.
