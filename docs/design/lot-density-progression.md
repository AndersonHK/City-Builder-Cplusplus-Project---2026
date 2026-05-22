# Lot Density Progression And Balancing

Use this guide when changing RCI constructor density caps, lot capacities, redevelopment scoring, desirability, local land-value effects, or the way neighborhoods progress from rural parcels to houses, rowhouses, apartments, midrises, and towers.

## Intent

- Preserve the SC4 feeling of staged urban growth without copying SC4's exact stage tables.
- Make city growth feel like a sequence of urban forms: rural holdings, detached houses, compact lots, rowhouses, walkups, apartment blocks, midrises, then towers.
- Prevent the highest unlocked density from spreading evenly across every sufficiently desirable tile.
- Let dense centers emerge from local synergy: land value, access, nearby completed density, and low pollution should reinforce each other.
- Keep the numbers feeling right before they are realistic. The player should perceive natural progression and variety at a glance.

## Scale Rule

All footprint and density examples in this document use project tiles. This project uses tiles that are roughly half the length of an SC4 tile, so SC4 lot intuition should be doubled in both dimensions:

| SC4 comparison | Project equivalent | Art-direction meaning |
| --- | ---: | --- |
| SC4 `1x2` | project `2x4` | narrow-front, deep-lot house or shop |
| SC4 `2x2` | project `4x4` | first apartment, walkup, courtyard, or small block |
| SC4 `2x3` | project `4x6` | deeper walkup, rowhouse group, or small apartment block |
| SC4 `3x2` | project `6x4` | width-wise merged shallow apartment or tower lot |
| SC4 `4x4` | project `8x8` | high-density block, civic anchor, tower base, or large industrial site |

This is a balancing rule, not just an art note. A project `4x4` apartment is emotionally a small urban building, not a huge square lot.

## SC4 Lesson

SC4 uses lot growth stages to pace development. Community references describe Residential and Commercial as having eight stages, Industrial as having three, and stage availability as being determined by local and regional population. Zoning density then limits how far a lot can progress: low density stops earlier, medium density reaches mid stages, and high density can reach the top stages.

The useful design lesson is not the exact table. It is the combination of:

- **city maturity:** the region unlocks higher forms over time
- **stage variety:** low and middle forms stay present instead of vanishing instantly
- **redevelopment:** lower forms can merge or upgrade into higher forms

The flaw to avoid is stage smear. If any high-desirability lot can reach the highest currently unlocked stage, a mature city can distribute towers too evenly and lose the sense of a downtown.

## SC4 Lot-Size Pattern Checked

The local spreadsheet `D:/Downloads/SimCity 4 Info.xlsx` has an `RCI buildings` sheet with `LotX`, `LotY`, `Stage`, `RCIType`, and `Capacity` columns. Aggregating its `1051` RCI records shows that SC4's middle progression stays much finer-grained than a simple "stage 4 means big block" model.

Top normalized lot sizes by stage in that sheet:

| Stage | Top normalized SC4 lot sizes | Project-scale read |
| ---: | --- | --- |
| 1 | `2x3`, `2x2`, `1x2`, `1x3`, `3x3` | varied small houses and shops |
| 2 | `2x3`, `3x3`, `3x4`, `1x2`, `2x2` | still mostly small-grain lots |
| 3 | `2x3`, `3x4`, `4x5`, `4x4`, `3x3` | broader mix, but not dominated by towers |
| 4 | `1x2`, `1x1`, `2x2`, `2x3`, `1x3` | early midrise and offices remain narrow |
| 5 | `2x3`, `2x2`, `3x4`, `1x2`, `2x4` | denser, but small footprints persist |
| 6 | `2x3`, `2x2`, `2x4`, `1x2`, `3x3` | midrise growth still has narrow lots |
| 7 | `3x3`, `3x4`, `4x4`, `2x3`, `2x4` | high-rise lots widen, but not always square |
| 8 | `4x4`, `3x4`, `3x3`, `2x4`, `4x5` | top stage prefers larger lots |

Stage 4 is the key warning: normalized `1x2` is the most common Stage 4 footprint in the sheet, and exact `1x2` is the most common Stage 4 residential footprint. Office and service buildings at Stage 4 also lean heavily on tiny lots such as `1x1`, `2x1`, `1x2`, and `2x2`.

At this project's scale, that means `2x4`, `2x5`, `3x5`, and `3x6` should be valid early-apartment and small-office footprints. A `4x4` apartment block is only one path into urban density, not the first or only path.

## Current Shape

- The RCI constructor already gates growth through `Data/RCI/rci_tools.xml`.
- Each `rciGrowth` rule declares a desirability threshold and population-keyed `maxDensityPerTile` values.
- `SimulationRuntime::rciMaxDensityPerTile` linearly interpolates the citywide maximum density from current city population.
- `findRciConstructorLotAsset` picks the highest-capacity asset that fits the candidate rectangle, demand budget, and density limit.
- `rciDesirabilityForCandidate` is currently binary: candidates with car or pedestrian access score `100`; candidates without access score `0`.

The next design step should be localizing the density cap. Population should decide what the city can support. Local place quality should decide where the dense buildings actually appear.

## Current Residential Capacity Snapshot

These are current XML capacities, measured in residents per project tile. They are useful because they already contain the most important invariant: rowhouses are below walkups.

| Current lot | Footprint | Capacity | Density | Read |
| --- | ---: | ---: | ---: | --- |
| `rci_residential_2x2_lot` | `2x2` | 2 | 0.50 | trailer or starter house |
| `rci_residential_2x4_lot` | `2x4` | 7 | 0.88 | mature detached house |
| `rci_residential_3x4_lot` | `3x4` | 14 | 1.17 | compact apartment-ish placeholder |
| `rci_residential_4x4_lot` | `4x4` | 16 | 1.00 | rowhouse group |
| `rci_residential_4x4_walkup_lot` | `4x4` | 32 | 2.00 | first walkup apartment |
| `rci_residential_4x8_lot` | `4x8` | 80 | 2.50 | apartment block |
| `rci_residential_8x8_lot` | `8x8` | 160 | 2.50 | large apartment block placeholder |

Rule: the highest rowhouse density should remain below the lowest apartment density. A good target is rowhouses topping out around `1.75` residents per tile and apartments starting around `1.90` or `2.00`.

## Density Bands

Use these as emotional stages. They do not need to appear as a literal `stage` enum immediately, but XML assets should be authored so they fall cleanly into these ranges.

| Band | Form | Typical project footprints | Target density | Notes |
| ---: | --- | --- | ---: | --- |
| 0 | Rural holding | `2x8`, `3x8`, `4x8` | 0.20-0.55 | large yard, trailer, farmhouse, or sparse house |
| 1 | Detached house | `2x6`, `2x8`, `3x6` | 0.55-0.95 | suburban/rural default, deep parcel |
| 2 | Compact detached or duplex | `2x4`, `2x5`, `3x4`, `3x5` | 0.95-1.25 | lot splitting begins |
| 3 | Rowhouse, terrace, or dense flats | `2x4` strings, `2x5`, `3x4`, `3x5`, `4x4`, `4x6`, `5x4` | 1.25-1.75 | highest non-apartment band |
| 4 | Walkup apartment or small office | `2x4`, `2x5`, `3x5`, `3x6`, `4x4`, `4x6`, `5x4`, `5x6`, `6x4` | 1.90-2.50 | must clearly beat rowhouse density |
| 5 | Midrise apartment | `4x6`, `4x8`, `5x6`, `6x4`, `6x6`, `8x8` | 2.50-4.00 | stronger height and block presence |
| 6 | Tower or podium | `6x4`, `6x6`, `8x8` and larger | 4.00-8.00 | rare, central, and locally gated |

The exact numbers are deliberately soft. The gap between Band 3 and Band 4 is not soft: it is the visual guarantee that apartment blocks feel like a new urban form.

## Intermediate Footprints

Intermediate lot sizes are not noise. They are the shapes that make growth feel gradual.

| Footprint | Useful read | Progression role |
| ---: | --- | --- |
| `2x4` | compact house, narrow apartment, or small office | project-scale SC4 `1x2`, so it must remain useful into early midrise |
| `2x5` | deeper detached house or shop | keeps low-density parcels from all being identical |
| `3x4` | compact house, duplex, or small apartment | bridges detached housing and rowhouse fabric |
| `3x5` | deeper duplex, small court, or mixed house lot | lets mature suburbs densify without becoming blocks |
| `3x6` | narrow deep apartment, office, or old urban flats | supports SF/NY-style 1900s apartment blocks without wide frontage |
| `5x4` | shallow merged rowhouse, walkup, or corner building | width-wise consolidation in shallow urban blocks |
| `5x6` | apartment, small office, or mixed-use block | transitional form before large block assets |
| `6x4` | shallow high-density apartment or tower lot | project-scale equivalent of SC4's common `3x2` high-stage tower pattern |

The constructor should not prefer only neat powers of two or squares. If the available sources form a `5x4`, `5x6`, or `6x4`, the asset library should eventually have believable forms for those footprints.

## Narrow Midrise Rule

Footprint and density band are related, but they are not the same thing.

- A project `2x4` can be a compact house, a rowhouse pair, a narrow walkup, or a small office depending on its module, height, coverage, and capacity.
- Early Stage-4-like growth should introduce narrow apartment and office assets before relying on `4x4` apartment blocks.
- A narrow apartment on `2x4`, `2x5`, or `3x6` should get its density from verticality and lot coverage while preserving narrow frontage.
- Brownstone apartments, San Francisco flats, main-street office slivers, and small Asian or Latin American mixed-use buildings are all valid references for this band.
- The rowhouse ceiling still applies: if a narrow asset is visually a rowhouse or terrace, keep it below the apartment density floor; if it is visually a walkup or midrise, it should clear the apartment floor.

## Citywide Unlock Curve

Population should remain the broad maturity gate. The current XML curve is short and test-friendly: residential density is `1.0` at population `0`, `2.0` at `2000`, and `3.0` at `10000`.

For a fuller progression, start from this prototype anchor table and adjust after playtesting:

| Population | City max density | New form that starts feeling legal |
| ---: | ---: | --- |
| 0 | 0.70 | rural holdings and trailers |
| 250 | 0.95 | detached houses |
| 1,000 | 1.20 | compact detached and duplexes |
| 3,000 | 1.75 | mature rowhouses, but no apartments yet |
| 7,500 | 2.10 | first walkup apartments in the best spots |
| 15,000 | 2.75 | apartment blocks become normal in strong centers |
| 35,000 | 4.00 | midrises become possible |
| 80,000 | 6.00 | first towers become possible |
| 200,000 | 8.00 | mature high-density core |

This should stay data-driven through `maxDensityPerTile` points and linear interpolation. I tried fitting the anchors as a single regression against `log10(population + 100)`. A straight line missed both the low and high ends; a quadratic fit was closer:

```text
cap ~= 7.20 - 5.02 * log10(population + 100) + 0.968 * log10(population + 100)^2
```

That fit is useful for eyeballing future curves, but it should not replace the XML anchor table. Piecewise linear anchors are easier to tune and easier to reason about when a player says a city is growing too quickly or too slowly.

## Local Density Cap

The citywide cap should be only the upper envelope. Each candidate should receive a local intensity score, then use that score to reduce or slightly emphasize the citywide cap.

Prototype formula:

```text
placeIntensity01 =
    0.30 * landValue01 +
    0.25 * access01 +
    0.30 * nearbyDensity01 +
    0.10 * civicAmenity01 -
    0.25 * pollution01 -
    0.10 * edgePenalty01

localMaxDensityPerTile = cityMaxDensityPerTile * (0.35 + 0.90 * clamp01(placeIntensity01))
```

Interpretation:

- A weak place reaches only about `35%` of the citywide cap.
- An ordinary place reaches about `60%` to `85%`.
- A strong place reaches about `100%` to `115%`.
- An exceptional core can slightly exceed the citywide cap, but only if that feels good and only within the current unlocked band.

The values should be normalized per city or by rolling percentiles, not by raw world-scale constants. Land value currently starts as a large integer and diffuses over time, so direct hardcoded thresholds would be brittle.

## Local Score Examples

Assume a citywide cap of `3.0` residents per project tile.

| Place | Land value | Access | Nearby density | Pollution | Edge penalty | Score | Local cap | Likely result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Rural edge | 0.20 | 0.20 | 0.05 | 0.05 | 0.70 | 0.04 | 1.16 | houses and compact lots |
| Ordinary suburb | 0.45 | 0.45 | 0.20 | 0.05 | 0.30 | 0.28 | 1.80 | compact houses, duplexes, maybe rows |
| Stable streetcar block | 0.60 | 0.70 | 0.55 | 0.10 | 0.10 | 0.52 | 2.44 | rows and first walkups |
| Main street center | 0.75 | 0.85 | 0.75 | 0.08 | 0.05 | 0.68 | 2.88 | walkups and apartments |
| Downtown core | 0.90 | 0.95 | 0.95 | 0.08 | 0.00 | 0.83 | 3.30 | strongest unlocked blocks |

This is the core anti-smear mechanism. A mature city can support density, but only places with enough accumulated urban advantage actually use it.

## Nearby Density Synergy

Nearby density should use completed lots, not under-construction lots.

Recommended first pass:

- Radius: `8` to `12` project tiles.
- Weight: inverse distance or two rings, with nearby lots weighted much more than distant lots.
- Value: completed capacity divided by completed lot area, normalized against the current citywide cap.
- Exclusions: ignore the candidate's consumed built capacity when scoring its replacement, or it may self-justify every redevelopment.
- Decay: pollution, abandonment, or poor access should pull the score down faster than density pulls it up.

This creates a natural downtown effect. Dense lots make adjacent lots more eligible for density, but only when the area also has land value, access, and tolerable pollution.

## Stage-Like Mix Targets

Use stage-like mix targets as a soft selection bias, not as a hard global quota. The constructor should prefer candidates that move the city toward these mixes, while local caps still decide where dense forms can appear.

| City cap | Rural | Detached | Compact | Rowhouse | Walkup | Midrise | Tower |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.70 | 70 | 30 | 0 | 0 | 0 | 0 | 0 |
| 1.20 | 15 | 55 | 30 | 0 | 0 | 0 | 0 |
| 1.75 | 5 | 25 | 45 | 25 | 0 | 0 | 0 |
| 2.50 | 0 | 10 | 25 | 40 | 25 | 0 | 0 |
| 4.00 | 0 | 0 | 10 | 25 | 45 | 20 | 0 |
| 6.00 | 0 | 0 | 5 | 10 | 30 | 45 | 10 |
| 8.00 | 0 | 0 | 0 | 5 | 15 | 45 | 35 |

These numbers are percentages of newly selected growth pressure, not a demolition command. Old neighborhoods should persist unless redevelopment pressure, demand, local cap, and candidate quality all justify replacement.

## Redevelopment Progression

The desired residential path is:

```text
rural holding -> deep detached lot -> split narrow lot -> compact detached/duplex -> rowhouse -> walkup -> midrise -> tower
```

Important rules:

- Do not jump directly from deep rural parcels to towers.
- Lot splitting should happen before apartment blocks dominate.
- A project `4x4` should be the first meaningful apartment footprint, matching the project's half-scale tile rule.
- Merging parcels can happen lengthwise or width-wise. Width-wise merging is especially important for shallow blocks where several narrow frontages combine into a `5x4`, `6x4`, or similar high-density lot.
- Width-wise merged towers and midrises should preserve frontage rhythm through facade bays, stoops, porches, entries, awnings, podium modules, or roof modules.
- Replacing built lots should require both net capacity improvement and a better local cap than the old form needed.

The constructor already consumes whole source rectangles and can merge adjacent sources. The future scoring layer should make the merge feel like a morphological next step, not merely the largest available rectangle.

## Candidate Selection Order

A future constructor pass should evaluate candidates roughly in this order:

1. Reject candidates without road or pedestrian access.
2. Compute citywide density cap from population.
3. Compute local intensity from land value, access, nearby density, civic amenity, pollution, and edge penalty.
4. Convert citywide cap to local max density.
5. Reject assets whose capacity per tile exceeds the local max.
6. Reject rowhouse assets above the rowhouse ceiling and apartment assets below the apartment floor.
7. Score remaining candidates by net capacity gain, local form fit, stage-mix pressure, and morphology preference, including a positive bias for plausible intermediate and width-wise merged footprints.
8. Choose among near-ties by seeded variation so blocks retain controlled chaos.

The current implementation already performs steps 1, 2, 5, and part of 7 in simpler form.

## XML Direction

Future lot XML should eventually expose density metadata directly instead of inferring everything from capacity:

```xml
<density band="rowhouse" minPerTile="1.25" maxPerTile="1.75" />
<morphology frontage="narrow" parcel="deep" redevelopment="split" />
```

This would let the constructor distinguish a dense rowhouse from a small apartment even when both have similar raw capacity. Until then, capacity-per-tile and footprint shape are enough for first tuning.

## Code Anchors

- `City Builder/Data/RCI/rci_tools.xml:4` through `City Builder/Data/RCI/rci_tools.xml:11` define current RCI growth rules and population-density anchors.
- `City Builder/SimulationRuntime.cpp:2918` applies `rciMaxDensityPerTile` while evaluating RCI candidates.
- `City Builder/SimulationRuntime.cpp:3122` and `City Builder/SimulationRuntime.cpp:3131` constrain selected constructor assets by demand and density capacity limit.
- `City Builder/SimulationRuntime.cpp:3207` interpolates max density per tile from city population.
- `City Builder/SimulationRuntime.cpp:3238` is the current binary desirability hook and the natural place to begin local intensity work.
- `City Builder/Tile.h:10` and `City Builder/Tile.h:11` provide land value and air pollution inputs for local scoring.
- `City Builder/RciTool.cpp:471` and `City Builder/RciTool.cpp:502` are the current parcel/block partitioning paths that should preserve the rural-to-narrow-lot progression.
- `City Builder/TransportNetworkTests.cpp:1567` through `City Builder/TransportNetworkTests.cpp:1576` cover current XML density interpolation expectations.

## Related Guides

- `docs/design/city-morphology-art-direction.md` owns the visual morphology target and half-scale tile convention.
- `docs/design/lots.md` owns lot/module placement, RCI parcel records, construction, and redevelopment constraints.
- `docs/design/xml-assets.md` owns XML validation and future schema changes.
- `docs/design/transport-network.md` owns access and commute topology, which should feed local intensity later.

## Sources Checked

- SC4D Encyclopaedia, "Growth Stage": https://wiki.sc4devotion.com/index.php?title=Growth_Stage
- StrategyWiki, "SimCity 4/Zoning and Demand": https://strategywiki.org/wiki/SimCity_4/Zoning_and_Demand
- SimCity Wiki, "Growable building": https://simcity.fandom.com/wiki/Growable_building
- Local spreadsheet: `D:/Downloads/SimCity 4 Info.xlsx`, sheets `RCI buildings` and `stage limits`

These are design references for understanding SC4's progression feel. They are not implementation dependencies and should not be treated as exact balancing requirements.

## Checks

- At low population, residential growth should prefer deep parcels and detached forms, not immediately compact city blocks.
- At medium population, the city should visibly split parcels and produce rowhouse streets before apartments dominate.
- Early midrise and small-office growth should be able to appear on narrow-front project `2x4`, `2x5`, and `3x6` lots.
- The densest rowhouse asset should remain below the least dense apartment asset.
- Mature cities should produce dense centers near accumulated value and access, not a uniform map-wide spread of the highest unlocked form.
- A new empty area in a mature region should not instantly become a downtown unless local value and access already justify it.
- Repeated zoning should produce a mixed district with old lower-density fabric, not a single current-best asset everywhere.
