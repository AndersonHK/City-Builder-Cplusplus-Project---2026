# Visual asset pipeline

Implemented revision: 2026-09-05. The physical contract is [6 m per tile](metric-art-standard.md).

## Authored data and cooking

`City Builder/Data/Modules/*.xml` contains stable module IDs, simulation stats,
and metric render recipes. The catalog has 86 modules, including six additional driveway, commercial parking and industrial service pieces. Two
legacy rowhouse IDs remain available for save compatibility. `Data/Lots` contains the existing lot layouts;
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
1x1 middle pieces and left/right/both-side path caps; cars are never stretched with
length. Old short path and driveway instances are replaced in the presentation
snapshot. Access reserves a 3x3 grid of 2 m sub-tiles per game tile before optional
landscaping is admitted. Props can share a tile with paths when their ground
footprints occupy different cells; nearby free sub-tiles are tried before a
conflicting optional prop is omitted. Fixed props remain routing obstacles.

## Declared visual variations

A metric module can declare up to 32 weighted variations. With a declared list,
only entries in that list participate; without one, the base mesh is used.
Variations inherit the module's physical dimensions, doors and simulation data.
They do not change capacity, effects, occupied tiles or the save format.

```xml
<variations>
  <variation id="brick" weight="3" seed="1" wallMaterial="brick"
             colorR="0.48" colorG="0.33" colorB="0.26" />
  <variation id="cladding" weight="1" seed="2" wallMaterial="metal"
             colorR="0.64" colorG="0.66" colorB="0.63" />
</variations>
```

`wallMaterial` accepts `inherit`, `brick`, `render`, or `metal`. `carStyle`
accepts `sedan`, `wagon`, or `pickup`; `treeStyle` accepts `oak`, `birch`, or
`conifer`. RGB overrides are optional and must be 0–1. Weight is 1–10000;
seed is 0–1000000. IDs must be unique lowercase letters/digits/underscore/hyphen.
A seed also varies service-pad arrangements and parking occupancy. Family-specific
styles apply only to geometry used by that family. Changing a material selects
the actual brick, plaster or metal texture layer, not just a tint.

Cooking writes `metric_<module>__<variation>` and its `_distant` partner.
The runtime hashes lot identity, module identity and lot-relative placement
coordinates. Choices remain deterministic across redraws and save/reload;
editing the declared list or weights intentionally changes the selection pool.
The first sets cover house finishes, industrial facades, cars, trees and yard props.

## Sub-tile footprints and access ownership

Declare ground clearance in metres relative to the module's unrotated front:

```xml
<pathBlocker xMeters="2.25" zMeters="2.25"
             widthMeters="1.5" depthMeters="1.5" />
```

This tree trunk/root footprint owns the centre 2 m cell of a 6 m tile. Its canopy
can overhang a neighboring path above pedestrian height. Shrub beds and fence
lines use their own bounds. Multiple blockers are supported. They rotate and
scale with the module's presentation geometry. Missing footprints conservatively
block the whole module. `pathPassable="true"` on a render recipe is intended for
bare ground; never apply it to a solid prop to hide a collision.

The lot reserves its building and access cells first. Modules explicitly marked
`optionalLandscape="true"` can then occupy free sub-tiles. They try nearby 2 m
positions at unchanged model scale, with full mesh bounds kept inside the lot
and out of buildings. A conflicting optional prop is omitted from presentation;
its saved module and simulation contributions are unchanged. Fixed props own
the 2 m cells touched by their blockers and are routed around.

The ownership grid is separate from the finer pedestrian routing grid and
physical door/cap connections. Vehicle pavement is never a generic pedestrian
shortcut. Driveway caps provide explicit path portals; commercial aisles provide
marked crossings. Paths are checked against solid footprints at every supported
parcel size and orientation. Ground decoration is not used as a canopy-sized
collision box.

Industrial lots compose 1x1 commercial stall, aisle, crossing and planted-island
modules. Stalls and aisles occupy separate rows; buildings at the street can have
side parking courts. A clear service aisle, loading-door approach and equipment
pads replace the former residential driveway and repeated loose boxes. The
service variants include ribbed containers, vessels/pumps and pallet racks/skips.
All of this is visual reconstruction, including for existing saved lots.

## Runtime cost

Each module also cooks a `_distant` mesh with the same dimensions and silhouette.
The game switches to those meshes above 64 visible tiles and uses them for
region thumbnails. High-detail facades retain physical window/trim geometry;
distant facades use simple window planes. The expanded catalog, including all 82 declared variants, totals 584,822
near triangles versus 99,778 distant triangles (83% fewer across the complete
catalog). A placement selects one variant, rather than drawing them all. Draws remain
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
- Select a declared variation in the dropdown to preview it explicitly. Its RGB
  fields edit that finish. Add variant inserts an editable declaration in the XML;
  set its weight/material/styles there and Save XML. Automatic returns to weighted
  selection. The main Variation button rerolls the complete lot.
- Toggle **2 m sub-tiles / footprints** to inspect ownership cells and ground
  blocker outlines without treating the entire tree canopy as an obstacle.
- Saving stages the whole data tree, validates references and geometry, keeps
  an XML `.bak`, and replaces the source and catalog. Concurrent source changes
  and ID changes are rejected. Module edits affect every lot using that module.
- Deploy to game publishes saved definitions and cooked art next to the game.
  Restart the game to load them. Save/load of cities remains in the game.
- Export BMP captures the current OpenGL viewport. `--capture <directory>`
  exports every module, selected explicit variations, 8x8 industrial parcels,
  both lot orientations, compact parcels, a distant tower,
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

## Save-without-exiting regression

Region thumbnail generation temporarily imports another city into the live
runtime and borrows its GPU buffers. Saving now increments the render-state
revision after restoring an active city, and thumbnail rendering invalidates all
live renderer cache stamps. The next city frame uploads its own roads and lots.
The integration regression borrows the runtime for an empty thumbnail city,
then checks restoration of the complete city and cache invalidation without
leaving city mode.

Reviewed captures: [industrial lots and 2 m sub-tiles](../art/industry-subtile-review.png),
[declared car/tree variants](../art/declared-asset-variations.png), and
[the updated asset manager](../art/industry-asset-manager.png).

Final verification: 86 module definitions, 82 declared variants, both LODs,
3,180 supported parcel/rotation combinations, and 26,850 path segments checked
against solid footprints. Optional props are also checked against access-owned
2 m cells. RCI construction/access tests: 1,615 checks; renderer tests: 228;
save/load integration tests: 38. Editor save/backup/invalid-edit/concurrency/ID
checks passed using an isolated data fixture. Release/x64 game and AssetManager
were rebuilt and deployed to their canonical executable paths.
