# City Morphology Art Direction

Use this guide when designing low-poly buildings, lot footprints, parcel fitting, zoning growth, neighborhood composition, or camera-facing visual style.

## Intent

- Capture the emotional appeal of SC4-style cities without copying its assets, pixel-art constraints, or fixed-camera rendering tricks.
- Build cities that feel familiar, pleasant, and naturally composed from an isometric angle while still surviving a 3D free camera.
- Treat urban morphology as art direction: parcel grain, lot depth, frontage rhythm, setbacks, block structure, and asset repetition matter as much as individual building models.
- Prefer controlled chaos over hero-object composition. A city should read first as a coherent block fabric, then as individual buildings.
- Keep the art target between abstraction and realism: low-poly, readable, warm, and believable, not photorealistic or toy-like.

## Core Thesis

SC4 works because it compresses real city structure into a disciplined visual system. It does not merely have nice buildings. It has narrow lots, deep parcels, aligned streets, modest repeated materials, dense roof detail, consistent lighting, and enough abstraction for the player's imagination to fill in the rest.

Modern 3D city builders often lose that quality when buildings are treated as isolated square-footprint objects. The result can be technically detailed, but visually less city-like: each building announces itself, streetscapes lose rhythm, and blocks feel assembled from props rather than grown from property patterns.

For this project, the goal is to spiritually adapt SC4's controlled chaos and realistic block proportions into a 3D, low-poly, freecam environment.

## Tile Scale Note

All footprint sizes in this document use this project's tile scale unless explicitly labeled as SC4 scale. This project uses tiles that are roughly half the length of an SC4 tile, so an SC4 `1x2` lot reads approximately like a project `2x4` lot, an SC4 `2x2` lot reads like project `4x4`, an SC4 `3x2` tower lot reads like project `6x4`, and an SC4 `4x4` high-density lot reads like project `8x8`.

This matters for art direction. A project `4x4` apartment block should be read as the emotional equivalent of a small SC4 `2x2`, not as a huge square superblock. Likewise, a project `2x6` house parcel is still a narrow-front, deep-lot residential parcel, not an unusually thin building.

## Morphology Principles

### Parcel Grain

Favor narrow-front, deep-lot parcels as the default building unit.

- Rural and early detached residential should commonly use project footprints like `2x6`, `2x8`, `3x6`, `4x8`, and other deep parcels with generous yard space.
- Mature detached residential should compress toward `2x4`, `2x5`, `3x4`, and `3x5` before it jumps to rowhouses.
- Early midrise residential and small office buildings can still live on narrow-front deep lots like `2x4`, `2x5`, and `3x6`. Higher density does not automatically mean wider frontage.
- Intermediate footprints such as `2x5`, `3x4`, `3x5`, `5x4`, and `5x6` should be treated as important glue, not awkward leftovers. They let districts densify without jumping straight from detached houses to square apartment blocks.
- Low-rise commercial and mixed-use should prefer narrow street faces with deeper bodies, such as `2x4`, `2x6`, `3x4`, `3x6`, `5x4`, `5x6`, and corner-specific variants.
- Rowhouses, walkups, and first apartments should often appear as project `4x4`, `4x6`, `5x4`, `5x6`, and long bar forms, preserving facade rhythm so they do not read as isolated square objects.
- Large project `8x8` lots should be special cases: civic buildings, big-box retail, warehouses, schools, hospitals, towers on podiums, or late-stage high-density forms.

Square lots should not be the visual default. They make buildings read as individual objects from a distance, which weakens the city fabric.

Residential density categories should stay visually honest: houses, townhouses, and rowhouses are low density; walkups, apartments, and midrises are medium density; towers are high density. Rowhouses are attached narrow-front buildings and should never be authored as wide detached blocks. In deeper lots, use rear yards, courts, paths, and service space, or a second layer only when it still reads as rowhouse fabric.

### Narrow Midrise Lots

Midrise form should be allowed to emerge while the parcel grain is still narrow.

- SC4's early Stage 4 fabric uses many `1x2` and similarly small lots, including apartments and small offices. At this project's half-scale, that maps to `2x4` and nearby shapes.
- Old urban apartment types, such as San Francisco flats, New York brownstone apartments, narrow walkups, and small main-street offices, should fit on `2x4`, `2x5`, `3x5`, and `3x6` lots.
- The asset library should include narrow midrise modules whose density comes from height, lot coverage, and reduced yard space rather than from square footprints.
- A block should be able to progress from deep houses to narrow apartment buildings before any `4x4` or wider apartment block appears.

### Width-Wise Merging

Narrow-front parcels can legitimately merge sideways into wider buildings, especially in shallow urban blocks.

- A mature block does not always gain density by getting deeper. It can also combine several narrow frontages into one wider building while keeping the original facade rhythm visible.
- Apartment blocks, offices, and towers can grow from width-wise consolidation: for example, SC4 commonly produced `3x2` high-stage apartment towers in narrow two-tile-deep blocks, which maps roughly to a project `6x4` footprint.
- Wide-shallow high-density lots should still read as a merged row of former parcels. Use bay spacing, entries, party-wall hints, roof modules, storefront divisions, or podium bays to preserve the history of the frontage.
- Do not reject `5x4`, `6x4`, or similar wide-shallow footprints simply because they are not deep. They may be the right answer for an urban block with strong frontage and limited depth.

### Street Frontage

Buildings should address the street with a face, not sit as centered objects in an isolated tile.

- Put doors, porches, shops, awnings, stoops, garage fronts, loading bays, and signage on the lot's front edge.
- Make the front edge visually active even when the building body is simple.
- Keep front setbacks consistent within a neighborhood tier, with variation used sparingly.
- Use corner lots to turn the facade around the block instead of placing an ordinary building beside two roads.

The street wall can be loose in suburbs and tight downtown, but it should always have rhythm.

### Lot Depth

Deep lots imply private life behind the street face.

- Detached homes need backyards, side yards, driveways, garages, sheds, trees, patios, or garden patches.
- Rowhouses and walkups can use rear yards, service alleys, small courts, or rear additions.
- Commercial lots can use loading zones, rear parking, service doors, dumpsters, or utility clutter.
- Industrial lots can spend depth on yards, tanks, stacks, storage, rail spurs, or loading aprons.

The player does not need every detail explained. The important thing is that the lot has a body behind its face.

### Repetition With Variation

Use a small number of forms in many curated combinations.

- Reuse roof shapes, facade rhythms, window spacing, trim colors, tree types, fence styles, and yard props across many lots.
- Vary height, color, roof orientation, porch placement, window groupings, and accessory structures.
- Prefer family resemblance over maximum asset variety.
- Let neighborhoods gain richness from repeated small differences, not from every building being unique.

The best block should look inevitable, not random.

### Background Buildings

Most buildings should be good background actors.

- A city cannot be made only from landmarks and hero props.
- Ordinary houses, duplexes, row shops, garages, warehouses, walkups, and plain apartment bars carry the composition.
- Landmark buildings should be rare enough that ordinary fabric still controls the visual language.

This matters especially for low-poly art, where one overly distinctive asset can dominate a whole neighborhood.

## Low-Poly Style Direction

### Familiar Abstraction

The city should look like a simplified memory of a real place.

- Use real architectural cues: roof pitches, chimneys, cornices, storefront bays, garage doors, loading docks, water towers, HVAC boxes, fire escapes, balconies, stoops, sheds, and fences.
- Simplify shapes aggressively, but keep silhouettes specific.
- Avoid smooth, generic boxes unless they are intentionally modern or industrial.
- Prefer chunky details that still read from normal camera distance.

The viewer should recognize the building type immediately without needing photoreal materials.

### Shared Material Vocabulary

Keep assets inside a narrow material family.

- Residential: muted siding, stucco, brick, asphalt shingles, tile roofs, concrete walks, wood fences, green yards.
- Main street: brick, painted masonry, glass strips, awnings, simple signs, flat roofs, parapets, rooftop units.
- Industrial: corrugated metal, faded paint, concrete, asphalt, rust accents, gravel, loading doors.
- Civic: stone, brick, pale concrete, formal roofs, plazas, steps, symmetrical massing.

Strong color is welcome, but it should act as accent and identity rather than noise. Awnings, doors, signs, roof tiles, flowers, vehicles, and civic details are good places for saturation.

### Roofs Matter

The camera sees roofs constantly.

- Give roofs clear silhouettes and purposeful color.
- Add low-poly roof features: chimneys, vents, skylights, HVAC boxes, water tanks, small dormers, parapets, solar panels, access huts, antennae.
- Keep roof clutter consistent with building class and wealth tier.
- Use roof orientation to break repetition across repeated lots.

A block with good roofs will look rich from isometric distance even if facades are simple.

### Controlled Contrast

Avoid extreme contrast between neighboring ordinary buildings.

- Adjacent buildings can differ, but they should share scale, texture density, lighting assumptions, and material temperature.
- Do not mix unrelated asset families in the same neighborhood without a zoning or historical reason.
- Keep high-contrast colors and unusual silhouettes for landmarks, civic anchors, special industries, or district-defining buildings.

The city should feel composed by local rules, not by an asset browser.

## Block Composition

### Blocks Should Read Before Buildings

When zoomed out, the player should see block structure, tree canopy, roads, yards, roof fields, and density gradients before individual models.

- Many small parcels create visual frequency without requiring huge asset counts.
- Deep parcels create believable interior block texture.
- Shared setbacks and frontage rules create order.
- Repeated trees, driveways, fences, and accessory buildings create neighborhood grain.

This is the difference between a city and a collection of buildings.

### Suburban Blocks

Suburbs should use narrow residential frontage with meaningful depth.

- Prefer lots that are deeper than they are wide.
- Avoid centering every house on a square lawn.
- Vary house placement slightly: front setback, driveway side, garage position, roof direction, porch depth.
- Use backyard objects and tree cover to make the interior of the block dense.
- Let some lots have additions, sheds, pools, patios, gardens, or larger trees.
- Larger low-density lots should usually be the same primary home with more garden, backyard, driveway, or accessory space, not two detached homes merged into one lot.

The goal is not perfect subdivision neatness. It is a readable property fabric.

### Urban Low-Rise Blocks

Low-rise urban blocks should be frontage-heavy.

- Use tight building faces, party walls, small shops, rowhouses, walkups, and mixed-use corners.
- Let thin lots merge into longer buildings when density rises, but preserve facade rhythm through bays, doors, cornices, and roof modules.
- Use alleys, rear courts, small parking pockets, and service clutter to explain depth.

Urban fabric should gain intensity by increasing frontage continuity, not by simply scaling every lot into a square.

### Commercial And Industrial Edges

Commercial and industrial areas can use larger footprints, but still need parcel logic.

- Small commercial should stay narrow and deep.
- Big commercial can use larger lots, but should include parking, service access, signage, and road relationship.
- Warehouses should often be long bars, sheds, or yard-backed forms rather than square boxes.
- Industrial sites should use depth for storage and logistics.

Large lots are allowed when their use explains their shape.

## Freecam Constraints

SC4 could optimize everything for fixed isometric views. This project cannot. Assets must remain coherent when viewed from multiple angles.

### Design For Isometric First

The normal city-camera angle should receive the strongest composition.

- Check silhouettes at the default isometric angle before approving an asset.
- Make roof and front-side readability the priority.
- Ensure street-facing details are visible at gameplay distance.
- Keep rear and side faces clean enough that rotation does not reveal unfinished art.

The camera is free, but the city should still have a preferred viewing grammar.

### Avoid Billboard Thinking

Do not rely on one perfect facade.

- Model enough side and rear detail to support rotation.
- Use repeated material blocks and simple trim to wrap buildings convincingly.
- Avoid facades that look like flat stage sets when seen diagonally.
- Put at least one meaningful feature on non-front faces for larger buildings.

The player should be rewarded for orbiting, not punished for noticing the trick.

### Scale And Legibility

Low-poly assets must read at city scale.

- Doors, windows, roofs, and props should be slightly exaggerated.
- Thin trim that disappears at distance should be simplified into larger bands or color blocks.
- Avoid tiny noisy props unless they form a clear group pattern.
- Use height tiers and roof shapes to distinguish building classes quickly.

The camera should make the city more charming, not less readable.

## Asset Authoring Rules

- Author assets as families: detached houses, rowhouses, corner stores, main-street shops, walkups, small warehouses, industrial sheds, civic anchors.
- Each family should share scale, palette, roof language, and detail density.
- Build variants through interchangeable parts: roof type, facade color, porch, garage, bay window, awning, sign, yard prop, rear addition.
- Every lot should know its front. Art, access, props, and orientation should respect that front.
- Use setbacks and empty space intentionally. Empty lot area should usually become yard, parking, service, landscape, or construction staging.
- Avoid importing visually unrelated assets without normalization.
- Prefer believable filler over novelty.

## Implementation Implications

- RCI zoning should continue favoring narrow and deep candidate parcels, especially for low and medium density.
- The lot fitter should treat project `2x4`, `2x5`, `3x4`, `3x5`, `4x4`, `5x4`, `5x6`, `6x4`, and similar intermediate parcels as first-class, not edge cases.
- Redevelopment can merge parcels into larger buildings, but should preserve small-scale facade rhythm where possible.
- XML archetypes should encode front direction, access side, parcel use, setback style, yard/parking/service zones, and density tier.
- Future procedural generation should start from parcel morphology before choosing a facade.
- Renderer previews and asset review tools should include default isometric screenshots and at least one rotated freecam screenshot.

## Code Anchors

- `City Builder/RciTool.cpp:127`, `City Builder/RciTool.cpp:222`, and `City Builder/RciTool.cpp:471` contain the current segment, block-depth, and road-insertion partitioning logic that determines RCI parcel grain.
- `City Builder/RciTool.cpp:328` through `City Builder/RciTool.cpp:334` define the current default RCI depth and width preferences.
- `City Builder/Data/RCI/rci_tools.xml:2` and `City Builder/Data/RCI/rci_tools.xml:3` are the data-facing residential and industrial RCI tool sizes that should carry this morphology direction.
- `City Builder/Lot.h:56` through `City Builder/Lot.h:64` define explicit lot footprints, front direction, and access declarations.
- `City Builder/Lot.cpp:226` and `City Builder/Lot.cpp:245` are the current footprint/module placement entry points that preserve non-square parcels.

## Related Guides

- `docs/design/lots.md` owns lot/module placement, explicit footprints, access declarations, construction, and RCI parcel behavior.
- `docs/design/lot-density-progression.md` owns density progression, local density caps, rowhouse/apartment balancing, and stage-like redevelopment pacing.
- `docs/design/xml-assets.md` owns the XML schema and validation rules that should expose front direction, footprints, access, and future art-facing lot metadata.
- `docs/design/visual-asset-pipeline.md` owns model format, asset sourcing, licensing, and the broader low-poly stylized-realism target.
- `docs/design/renderer.md` owns camera, preview, and render-facing checks for whether the art direction reads at gameplay distance.

## Checks

- Compare a generated neighborhood against a real aerial view at similar scale. It should share parcel grain, lot depth, road rhythm, and tree/block texture even if the art is stylized.
- Zoom out until individual details blur. The block should still read as a coherent neighborhood.
- Rotate the camera around a block. Buildings should remain believable from side and rear views.
- Inspect a residential block for too many square lots, centered houses, identical setbacks, or empty unused yards.
- Inspect a commercial street for active frontage and corner behavior.
- Inspect an industrial area for logistics depth rather than isolated square sheds.
- Place many repeated assets together. The result should feel like a district, not obvious duplication.
- Place mixed assets together. The result should still share palette, scale, and detail density unless the district intentionally changes.

## North Star

The city should feel like a model of real urban property patterns, not a board of decorative pieces. If a randomly zoned block looks pleasant, varied, and inevitable, the art direction is working.
