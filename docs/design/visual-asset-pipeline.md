# Visual Asset Pipeline Notes

Snapshot: 2026-05-17

Use this guide when replacing placeholder lot/module prisms with textured 3D models, selecting model formats, sourcing assets, or changing the render-facing lot/module XML schema.

## Intent

- Move from placeholder boxes to authored textured 3D models without making rendering the source of simulation truth.
- Preserve the SC2000/SC4 emotional target: tile-legible cities, colorful realism, strong silhouettes, and buildings that compress a lot of civic personality into a small set of readable forms.
- Keep assets practical for a solo/non-artist workflow: source, kitbash, adapt, and validate rather than requiring a full custom art department.
- Keep licensing explicit and boring. Asset provenance should be easy to audit before anything ships.

## Recommended Direction

Use **glTF 2.0**, preferably binary **`.glb`**, as the first runtime/import format.

Keep **`.blend`** files as optional source art when an asset is authored or materially edited in Blender, but treat exported `.glb` files under `Data/Models` as the game-facing payload. Later, if loading speed or draw-call control becomes a bottleneck, add an offline cooker that converts `.glb` into a smaller engine-native mesh/material bundle. Do not start there.

Why glTF fits this project:

- It is an open, modern runtime delivery format rather than a DCC-only authoring format.
- It carries meshes, scene nodes, transforms, textures, samplers, and PBR metallic-roughness material data in a standard way.
- Blender has built-in glTF import/export support, so a non-artist can use Blender as the conversion and cleanup hub.
- Many asset sites either provide glTF directly or can be routed through Blender/Sketchfab export.
- `.glb` is convenient for this stage because geometry, material metadata, and embedded buffers can live in one file.

Avoid using these as the primary game-facing format:

- **OBJ:** useful for tiny experiments, but weak for a real material pipeline. It does not naturally represent modern PBR materials, scene hierarchy, variants, or animation.
- **FBX:** common in asset stores, but proprietary and exporter-dependent. Good as an import source into Blender, not as the engine contract.
- **USD/USDZ:** powerful for film/DCC pipelines, but too broad for this engine's immediate needs.
- **Raw downloaded marketplace zips:** acceptable as source intake, not runtime contract. Normalize them into the project's own scale, orientation, material, and license expectations.

## First Renderer Target

Do not jump directly to a full physically based renderer. The first useful milestone is:

- load static `.glb` meshes
- support positions, normals, UVs, indices
- support base color textures and base color factors
- support alpha only where explicitly needed, such as fences or foliage later
- use one simple directional/sun light plus ambient term or baked ambient occlusion
- keep placeholder prism fallback when a model is missing or invalid

PBR compatibility is still valuable even if the first shader is simpler. It keeps assets authored in a standard material vocabulary and leaves room for the later Vulkan renderer to grow into roughness, metalness, normals, AO, and emissive windows.

## Art Direction

The target is not photorealism. It is **readable stylized realism**:

- real architectural cues, simplified enough to read at the normal city-camera distance
- saturated but believable color, especially roofs, brick, painted facades, parks, signage, and industrial accents
- high-contrast roof shapes and silhouettes because the camera sees roofs often
- chunky details that survive distance: water towers, HVAC blocks, fire escapes, bay windows, awnings, chimneys, smokestacks, loading doors, rooftop machinery
- density tiers that feel like a city growing: trailers and small houses, row houses, walkups, apartments, warehouses, factories, civic anchors, midrise blocks
- no single asset should look like a randomly downloaded hero prop; assets need a shared scale, palette, lighting assumption, and texture density

SC4 is a north star for composition and feeling, not a source asset library. Do not rip, upscale, trace, or derive shipped content from SimCity or other copyrighted game art.

## Asset Sources

Best early sources:

- **Kenney:** Good first-pass CC0 city assets and road kits. Their support page says game assets are public-domain/CC0 and usable commercially without required attribution. Good for placeholders that are already friendlier than boxes.
- **Poly Haven:** CC0 textures, HDRIs, and some models. Especially useful for brick, concrete, roof, asphalt, grass, dirt, and industrial material references.
- **Quaternius:** CC0 low-poly/game-ready packs, often including OBJ/FBX/Blend/glTF depending on the pack. Strong for props, vehicles, and modular pieces.
- **Sketchfab:** Huge library and glTF export, but use carefully. Filter by downloadable and license. Prefer CC0 or clearly compatible CC BY. Avoid NonCommercial, NoDerivatives, fan/IP models, logos, recognizable brands, and real-world trademarked landmarks.
- **OpenGameArt:** Useful for free/open assets, but licenses vary widely. Treat each individual asset as a license review item.
- **KitBash3D or similar paid kits:** Useful for inspiration, kitbashing, or background city massing if the license fits. Their official docs distinguish free, subscription, and perpetual licensing. They also warn about real-world building rights, so avoid using recognizable buildings as focal assets.

Asset-source posture:

- Prefer CC0 for anything committed to the repo early.
- CC BY can be acceptable if attribution is tracked in a third-party notices file.
- Avoid GPL/LGPL art assets for now because the redistribution obligations are easy to misunderstand in a game asset pipeline.
- Avoid assets based on existing games, movies, brands, sports teams, franchises, or specific real buildings unless there is explicit clearance.
- Keep an `asset_credits.md` or equivalent before importing non-CC0 assets.

## Google Earth-Like Building Data

The low-poly/photogrammetric buildings in Google Earth are an excellent visual reference, but Google Earth/Maps content should not be treated as a reusable source asset library. Google Maps Platform terms prohibit caching Google Maps content except where specifically permitted and prohibit creating content from Google Maps content, including 3D building models from 45-degree imagery. Google Earth Studio's FAQ also says Google does not currently offer a license to use Earth imagery for commercial applications.

Do not rip, scrape, reconstruct, trace, or photogrammetry-capture Google Earth/Maps 3D buildings for this project.

There is still a promising open-data version of the idea:

- **OpenStreetMap:** Open building footprints plus some 3D tags such as `height`, `building:levels`, `building:part`, `roof:shape`, `building:material`, and colors. OSM is ODbL data, so attribution and share-alike database obligations matter.
- **OSM2World:** Open-source converter that turns OSM data into 3D models and exports glTF/glb/OBJ. Useful as a reference renderer, prototype exporter, or inspiration for a procedural building generator.
- **Overture Maps buildings:** Cloud-native global building dataset. Its buildings theme explicitly supports 2D and 3D/2.5D visualization by extruding building heights/levels and is ODbL because it is strongly OSM-derived.
- **Microsoft Global ML Building Footprints:** Global building footprint polygons, with some recent height estimates, released under CDLA Permissive 2.0 as of the current repository docs.
- **Google Open Buildings:** Open footprint dataset for the Global South; Google says it is not the same as Google Maps buildings, though there is some overlap. The dataset is dual-licensed CC BY 4.0 and ODbL, and the 2.5D temporal dataset adds estimated building heights.
- **CityGML / CityJSON open city models:** Some municipalities publish actual 3D city models, often LoD1/LoD2. Examples include New York City and Helsinki. These are closer to "real 3D building data" than ordinary footprints, but each city has its own format, coordinate system, geometry quality, and license.

Best use for this project:

- Use open geospatial data as **reference and procedural source material**, not as the main art library.
- Generate city-block massing studies: real block sizes, building footprints, height distributions, roof forms, and downtown density patterns.
- Convert small downtown samples through OSM2World or CityJSON tooling to study silhouettes, camera readability, roof density, and scale.
- Derive a fictional building grammar from open patterns: narrow row houses, corner shops, warehouse bars, slab apartments, towers on podiums, rail/industrial sheds.
- Keep shipped game buildings fictional and normalized to the XML lot/module system, even if their proportions are inspired by open geospatial data.

This path is especially attractive for tools. A future "city-shape sampler" could ingest an open building dataset, classify footprint/height archetypes, and output candidate lot/module families without copying any Google-owned mesh or imagery.

## Candidate-To-Model Pipeline

The city-shape sampler should not output finished art directly. It should output **model recipes**. A separate offline asset generator should turn those recipes into `.glb` files plus XML asset definitions that the game already knows how to load.

Recommended flow:

1. **Sample open city data:** read OSM/Overture/CityJSON/etc. into local normalized footprints, heights, roof tags, land-use hints, and block context.
2. **Classify shape archetypes:** assign labels such as rowhouse, detached house, walkup, slab apartment, tower, warehouse, factory, civic block, corner commercial, park structure, or utility building.
3. **Snap to game lots:** convert real-world dimensions into game tile/module footprints, front direction, access side, density tier, and zoning type.
4. **Emit recipe JSON:** save a stable intermediate record with archetype, footprint, height/floors, roof type, facade rhythm, material palette, style tags, and source statistics.
5. **Generate mesh offline:** feed the recipe to a procedural model builder that creates the actual geometry, UVs, materials, and optional texture atlas assignment.
6. **Export `.glb`:** write the model to `Data/Models/...` using Blender's glTF exporter or a direct glTF writer.
7. **Post-process:** run glTF optimization/validation, deduplicate data, resize textures, and optionally generate WebP/KTX2 later.
8. **Emit XML:** write or update module/lot XML so the generated model is referenced by a strict asset id.
9. **Preview and curate:** render thumbnail/contact-sheet previews, reject ugly or invalid results, and hand-promote good candidates into the authored asset set.

Example recipe shape:

```json
{
  "id": "res_rowhouse_2x4_a",
  "archetype": "rowhouse",
  "zoningType": "residential",
  "footprintTiles": { "width": 2, "depth": 4 },
  "floors": 3,
  "front": "south",
  "roof": { "shape": "flat", "equipment": "light" },
  "facade": { "bays": 2, "windowRows": 3, "shopfront": false },
  "palette": "brick_warm_blue_roof",
  "wealthTier": "low",
  "densityTier": "medium",
  "sourceStats": {
    "sampleCount": 184,
    "medianFootprintAreaM2": 92,
    "medianHeightM": 10.5
  }
}
```

The model generator can be built in two stages:

- **Blender-backed generator first:** a Python script runs Blender in background mode, creates meshes/materials from recipes, and calls the glTF exporter with `GLB` output. This is the most forgiving path because the results are inspectable and editable in Blender, and it avoids writing a full custom glTF authoring stack before the art direction is proven.
- **Direct generator later:** once the grammar is stable, generate `.glb` directly with a library such as glTF-Transform, cgltf write support, or a small custom writer. This removes the Blender dependency from batch generation, but it is less pleasant while the shapes/materials are still changing.

The generator should be grammar-based, not a "make arbitrary building" black box:

- body mass: box, L-shape, courtyard, podium + tower, warehouse bar, shed
- roof grammar: flat, gabled, hipped, mansard, sawtooth, skylight strip
- facade grammar: floor bands, window columns, shopfronts, loading doors, balconies, fire escapes
- detail kit: HVAC blocks, tanks, chimneys, signs, awnings, roof rails, vents, smokestacks
- material kit: brick, painted plaster, concrete, glass, asphalt roof, tile roof, metal siding
- palette kit: a restrained set of color families shared across all generated assets

This keeps the output fictional but urban-plausible. The open city data teaches proportions and density; the generator supplies the game's art style.

The runtime engine should only consume the final `.glb` and XML. It should not depend on Blender, OSM, Overture, or any geospatial parser.

## Style Normalization

Downloaded assets will not share an art direction by default. Normalize them in Blender before export:

- set a consistent scale: one game tile maps to one authored asset unit convention
- set a consistent up-axis/front convention
- move origin/pivot to the lot/module anchor point
- apply transforms before export
- simplify overly dense meshes
- remove hidden/interior geometry that cannot be seen from the city camera
- bake procedural materials into image textures
- reduce texture sizes aggressively for small buildings
- tint/repaint materials toward the shared palette
- add simple rooftop detail to otherwise plain boxes
- create two or three color variants for repeated residential/commercial buildings

For the SC4-like feel, consistency matters more than raw model fidelity. A low-poly building with intentional color, readable windows, and a strong roofline will usually beat a random photogrammetry building with muddy textures and millions of triangles.

## XML Schema Direction

Current module XML has placeholder render data:

```xml
<render height="0.7" colorR="0.55" colorG="0.67" colorB="0.78" />
```

Evolve this without breaking fallback behavior:

```xml
<render height="0.7" colorR="0.55" colorG="0.67" colorB="0.78"
        model="Models/Residential/house_2x2_a.glb"
        modelScale="1.0"
        modelYawDegrees="0"
        variant="blue_roof" />
```

Better long-term shape, once the simple attribute path proves useful:

```xml
<render height="0.7" colorR="0.55" colorG="0.67" colorB="0.78">
  <model path="Models/Residential/house_2x2_a.glb"
         scale="1.0"
         yawDegrees="0"
         variant="blue_roof" />
</render>
```

Rules:

- `height` and color remain valid fallback/debug values.
- Missing `model` keeps the current prism path.
- A missing model file should fail asset validation in strict mode once the feature is no longer experimental.
- Rotation-aware lot/module placement should rotate the model transform exactly as it rotates the module footprint and access declarations.
- Model bounds must not silently exceed the module footprint unless the XML explicitly opts into overhangs.

## Render Data Direction

Add a presentation-only model handle to module/lot render data. The simulation should not know about GPU buffers, texture objects, material instances, or draw calls.

Likely data split:

- `AssetLoader`: parse XML model references and validate paths.
- `ModelCatalog` or `MeshAssetCatalog`: load `.glb`, deduplicate meshes/materials/textures, and own CPU-side model metadata.
- `Lot` or snapshot build path: emit model instance records with transform, construction progress, tint/variant, footprint bounds, and model handle.
- `Renderer`: upload immutable mesh/material GPU resources, then draw instance records from published snapshots.

Keep construction growth as a presentation transform:

- Current construction lots scale module render height from 0 percent to full height.
- Mesh-backed modules can reuse this by scaling the model on the vertical axis or by selecting a construction placeholder model.
- Long term, construction scaffolding can be its own model/variant.

## Implementation Phases

1. **Documentation and asset trial:** choose 3 to 5 CC0 `.glb`/`.blend` assets and normalize them in Blender. Target one house, one apartment/walkup, one warehouse, one smokestack/factory, and one park prop.
2. **Loader spike:** load one `.glb` through a small C/C++ glTF loader, extract positions/normals/UVs/indices, and render it in place of one module prism.
3. **XML link:** add optional model path attributes to `<render>`, preserving prism fallback.
4. **Material minimum:** support base color texture/factor and simple lighting. Add normal/roughness later.
5. **Batching:** cache model GPU resources and draw repeated instances without re-uploading mesh data.
6. **Chunk ownership:** after the feature is visually proven, decide whether model instances should join chunk-owned render data with the rest of the visible-dirty renderer strategy.
7. **Texture compression:** later, support KTX2/Basis Universal for production-sized texture sets. Do not block the first mesh milestone on this.

## C++ Loader Recommendation

For this Visual Studio project, start with either:

- **cgltf:** very low dependency pressure, single-file C99, supports core glTF/GLB and many extensions. Good for a controlled engine pipeline where image decoding and GPU upload are engine-owned.
- **fastgltf:** modern C++17, fast, broader helper tooling, and a good fit if the project is comfortable adding the dependency path cleanly.

TinyGLTF is also viable, but it brings its JSON/image dependency shape with it. Assimp is useful as an offline converter or diagnostic tool, but it is probably too large and format-broad for the first runtime contract. Prefer one good glTF path over "support every 3D format" at runtime.

## Performance Budgets

Initial practical targets:

- small residential modules: hundreds to a few thousand triangles
- larger buildings: low tens of thousands only when visually justified
- avoid unique 4K textures for ordinary buildings
- prefer 512 or 1024 textures for small/medium repeated buildings
- favor repeated model instances and color/material variants over hundreds of one-off meshes
- remove backsides/interiors that cannot be seen from the camera
- author LOD later only after the first model path works

The game can eventually show a huge number of lots. Repetition, batching, culling, texture reuse, and a consistent kit language are more important than asset-by-asset beauty.

## Open Questions

- What is the exact tile-to-world unit convention for authored models?
- Should model origin be the module anchor, lot anchor, or footprint center?
- Should overhangs be allowed for roofs/awnings, and if so how are they represented in culling bounds?
- Should residential variation be modeled as separate `.glb` files, glTF material variants, XML-chosen tints, or a mix?
- How should asset credits be stored once non-CC0 assets enter the repo?
- Should the first model loader live inside `AssetLoader`, or should XML loading only collect paths and a separate renderer-facing model catalog load mesh data?

## Sources Checked

- Khronos glTF overview: https://www.khronos.org/gltf/
- Khronos glTF 2.0 specification: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- Khronos KTX overview: https://www.khronos.org/ktx/
- Khronos `KHR_texture_basisu` extension: https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_basisu/README.md
- Khronos Blender glTF I/O repository: https://github.com/KhronosGroup/glTF-Blender-IO
- Blender glTF manual: https://docs.blender.org/manual/en/5.0/addons/import_export/scene_gltf2.html
- cgltf: https://github.com/jkuhlmann/cgltf
- fastgltf: https://github.com/spnda/fastgltf
- TinyGLTF: https://github.com/syoyo/tinygltf
- Kenney support/license FAQ: https://kenney.nl/support
- Kenney City Kit example: https://www.kenney.nl/assets/city-kit-commercial
- Poly Haven license: https://polyhaven.com/license
- Quaternius public transport pack example: https://quaternius.com/packs/publictransport.html
- Sketchfab glTF page: https://sketchfab.com/features/gltf
- Sketchfab license agreement: https://sketchfab.com/licenses
- KitBash3D license overview: https://kitbash3d.com/pages/licenses
- Google Maps Platform Terms of Service: https://cloud.google.com/maps-platform/terms
- Google Earth Studio FAQ: https://www.google.com/earth/studio/faq/
- OpenStreetMap copyright/license: https://www.openstreetmap.org/copyright
- OpenStreetMap Simple 3D Buildings: https://wiki.openstreetmap.org/wiki/Simple_3D_Buildings
- OSM2World: https://osm2world.org/
- Overture Maps buildings: https://docs.overturemaps.org/guides/buildings/
- Microsoft Global ML Building Footprints: https://github.com/microsoft/GlobalMLBuildingFootprints
- Google Open Buildings: https://sites.research.google/gr/open-buildings/
- CityJSON overview: https://www.cityjson.org/about/
- NYC 3-D Building Model listing: https://catalog.data.gov/dataset/3-d-building-model
- Helsinki 3D: https://www.hel.fi/en/decision-making/information-on-helsinki/maps-and-geospatial-data/helsinki-3d
- Blender command line arguments: https://docs.blender.org/manual/en/latest/advanced/command_line/arguments.html
- Blender glTF export operator: https://docs.blender.org/api/current/bpy.ops.export_scene.html
- glTF-Transform: https://gltf-transform.dev/
