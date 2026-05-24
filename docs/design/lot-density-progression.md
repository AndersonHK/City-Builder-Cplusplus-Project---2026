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
- Each XML-defined zone declares its zoning color, desirability threshold, and population-keyed `maxDensityPerTile` values.
- Zones and RCI types are separate concepts. A zone is what the player paints and what owns zoning color, parcel sizing, and max density. An RCI type is what grows there and owns demand/desirability identity. Low-density residential and high-density residential currently both allow the same low-wealth residential RCI type, while industrial allows dirty industry.
- `SimulationRuntime::rciMaxDensityPerTile` linearly interpolates the citywide maximum density from current city population.
- `findRciConstructorLotAsset` picks the highest-capacity asset that fits the candidate rectangle, front direction, demand budget, and density limit.
- `SimulationRuntime::rciLocalMaxDensityPerTile` multiplies the citywide cap by normalized local land value, with a small starter-density floor. Land value 0 allows only low-density starter growth through that floor; land value at the displayed overlay cap gives 100 percent of the city cap.
- `rciDesirabilityForCandidate` is currently binary: candidates with car or pedestrian access score `100`; candidates without access score `0`.

The next design step should be localizing the density cap. Population should decide what the city can support. Local place quality should decide where the dense buildings actually appear.

## Current Template Snapshot

The checked-in RCI catalog is now template-shaped rather than legacy one-lot-per-size data. Low- and high-density residential zones both grow the same current low-wealth residential RCI type, while the zone density curve decides whether growth stops at rowhouse density or can continue into towers. Residential and industrial both provide two named templates for every footprint from `2x2` through `8x8`, for each `low`, `medium`, and `high` density band.

The important invariant is tested directly in `RciLotConstructionTests.cpp`: for each zoning type and footprint, no low template may be as dense as any medium template, and no medium template may be as dense as any high template. Residential module categories are also semantic:

- Houses, townhouses, rowhouses, terraces, duplexes, and trailers are `low`.
- Walkups, apartments, and midrises are `medium`.
- Towers are `high`.

Template lots should still look like lots. A project `2x4` detached house, `2x6` detached house, and `2x8` rural holding can share the same primary house module; the larger templates should spend the extra area on garden, backyard, path, driveway, and similar secondary modules rather than duplicating a second house.

Rule: the highest low-density residential template should remain below the lowest medium-density residential template for the same footprint, and the same ordering must hold between medium and high. Rowhouse modules must stay at three project tiles wide or narrower.

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

Population remains the broad maturity gate. The XML curve is intentionally longer than the first prototype so density progression stays smooth, and the maximum density cap is reached at one million people.

| Population | City max density | New form that starts feeling legal |
| ---: | ---: | --- |
| 0 | 0.70 | rural holdings and trailers |
| 250 | 0.95 | detached houses |
| 1,000 | 1.20 | compact detached and duplexes |
| 3,000 | 1.75 | mature rowhouses, but no apartments yet |
| 7,500 | 2.10 | first walkup apartments in the best spots |
| 15,000 | 2.50 | apartment blocks become normal in strong centers |
| 35,000 | 3.00 | early midrises become possible |
| 75,000 | 3.75 | stronger midrises become possible |
| 150,000 | 4.50 | first compact towers become possible |
| 300,000 | 5.50 | tower districts begin to form |
| 600,000 | 6.75 | mature high-density centers expand |
| 1,000,000 | 8.00 | mature high-density core |

This full citywide curve belongs to the high-density residential zone. Low-density residential currently uses the same early anchors through population 3,000 and then stays flat at `1.75`, keeping it below apartment density while still letting small parcels mature. This should stay data-driven through `maxDensityPerTile` points and linear interpolation. I tried fitting the anchors as a single regression against `log10(population + 100)`. A straight line missed both the low and high ends; a quadratic fit was closer:

```text
cap ~= 7.20 - 5.02 * log10(population + 100) + 0.968 * log10(population + 100)^2
```

That fit is useful for eyeballing future curves, but it should not replace the XML anchor table. Piecewise linear anchors are easier to tune and easier to reason about when a player says a city is growing too quickly or too slowly.

## Local Density Cap

The current implementation uses a simple land-value version of the local cap while the richer place-intensity formula below remains a design target:

```text
localLandValue01 = clamp01((averageCandidateLandValue - displayedLandValueMin) /
                           (displayedLandValueCap - displayedLandValueMin))

localMaxDensityPerTile = max(starterDensityFloor, cityMaxDensityPerTile * localLandValue01)
```

This means a candidate at land value `0` can use only the starter floor, and a candidate at the displayed overlay cap can use the full city cap. The displayed overlay cap is intentionally separate from the engine's possible integer land-value range.

This has a feedback risk: when city population crosses a new density anchor, the citywide cap rises, the constructor can choose denser residential assets, those assets emit larger land-value amounts, and that higher land value is multiplied back into later local caps. That is not a literal exponential formula in one function, but it is an iterative multiplicative feedback loop across redevelopment ticks.

The next formal model should keep land value as the slow local eligibility score, use desirability as a growth prerequisite, and avoid speculative pathfinding for every possible future building. New RCI growth should still require desirability `60`, but desirability should not partially multiply construction density below that threshold.

```text
effectiveMaxDensity =
    min(
        populationMaxDensity,
        starterDensityFloor
      + (populationMaxDensity - starterDensityFloor)
        * localIntensityFactor
        * demandPressureDensityFactor)
```

`localIntensityFactor` should be bounded `0..1` and should not be raw emitted land value from the candidate's own density. Nearby-density terms must normalize against the current citywide cap and should exclude the candidate's consumed source lots when scoring a replacement. That way a tower does not justify an even taller tower merely because it already exists.

Built RCI lots should recalculate their own average desirability every tick from the tiles they own, using the sensitivity tables for their RCI type and wealth/job class. Their active capacity should scale from `50%` at desirability `40` to `100%` at desirability `60`, and remain capped at `100%` above `60`:

```text
currentCapacityFactor =
    clamp(0.5 + 0.5 * ((averageDesirability - 40) / 20), 0.5, 1.0)
```

The lot still reserves or satisfies demand as if it had `100%` of its maximum capacity. Residents, workers, jobs, trips, pollution, and service consumption are proportional to current active capacity.

`demandPressureDensityFactor` should be per RCI type and modest:

```text
demandPressureRatio = unsatisfiedDemand / max(activeCapacityForThisRciType, seedCapacity, 1)
demandPressureDensityFactor = clamp(1.0 + demandPressureRatio * demandSlope, 1.0, demandPressureCap)
```

For example, `200` unmet residential demand against `1000` active residential capacity and a `0.5` slope gives a `1.10` density multiplier before XML caps and smoothing. Dirty industry should compare dirty-industry demand to existing dirty-industry capacity, not to total city population.

Desirability itself should come from RCI XML sensitivity tables over current tile fields and actual lot results. Low-wealth residences can love `parkEffect`, hate `airPollution`, and treat actual commute time as `+5` at `0%` of maximum commute, `0` at `50%`, and `-20` at `100%`. Dirty industry should initially have zero deltas for both park effect and air pollution, so it stays at baseline `60`. This avoids expensive hypothetical pathfinding. A far suburb can grow a few apartments if its land value is good and desirability reaches `60`, but those new residents add real commute load. If highways saturate or job access remains shallow, actual commute results lower residential desirability, new growth stops below `60`, active capacity falls, and skyscraper growth stalls naturally.

The citywide cap should be only the upper envelope. Each candidate should receive a local land-value/intensity score, then use that score to reduce or slightly emphasize the citywide cap. Park effect, air pollution, and future named service or nuisance fields should feed RCI desirability tables instead of being universal density-cap terms.

Preferred first prototype:

```text
nearbyDensity01 = clamp01(completedNearbyCapacityPerTile / max(cityMaxDensityPerTile, starterDensityFloor))

localIntensity01 =
    geometricMean(
        developedSite01 + epsilon,
        access01 + epsilon,
        nearbyDensity01 + epsilon,
        serviceSiteValue01 + epsilon)
    * (1 - edgePenalty01)

localMaxDensityPerTile =
    starterDensityFloor
  + (cityMaxDensityPerTile - starterDensityFloor)
    * smoothstep(localIntensity01)
```

Interpretation:

- A weak place reaches only about `10%` to `35%` of the citywide cap.
- An ordinary place reaches about `35%` to `75%`.
- A strong place reaches about `75%` to `100%`.
- An exceptional core reaches the full cap only when land value approaches the displayed overlay cap.
- Missing access, missing nearby density, or missing service support should pull the geometric mean down hard, so one strong factor cannot by itself unlock the whole citywide cap.

The values should be normalized through display caps or XML-authored field caps, not hidden engine integer limits. Land value currently starts as a large integer and diffuses over time, so direct hardcoded thresholds would be brittle.

## Local Score Examples

Assume a citywide cap of `3.0` residents per project tile. The `Land value` column is the normalized value after diffusion and target blending; park effect and pollution would be read separately by desirability tables.

| Place | Developed site | Access | Nearby density | Service site | Edge penalty | Land value | Local cap | Likely result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Rural edge | 0.10 | 0.20 | 0.05 | 0.00 | 0.70 | 0.08 | 0.52 | houses and compact lots |
| Ordinary suburb | 0.35 | 0.45 | 0.20 | 0.10 | 0.30 | 0.30 | 1.11 | compact houses, duplexes, maybe rows |
| Stable streetcar block | 0.55 | 0.70 | 0.55 | 0.20 | 0.10 | 0.50 | 1.65 | rows and first walkups |
| Main street center | 0.70 | 0.85 | 0.75 | 0.35 | 0.05 | 0.70 | 2.19 | walkups and apartments |
| Downtown core | 0.95 | 0.95 | 0.95 | 0.55 | 0.00 | 0.95 | 2.87 | strongest unlocked blocks |

This is the core anti-smear mechanism. A mature city can support density, but only places with enough accumulated urban advantage actually use it.

## Nearby Density Synergy

Nearby density should use completed lots, not under-construction lots.

Recommended first pass:

- Radius: `8` to `12` project tiles.
- Weight: inverse distance or two rings, with nearby lots weighted much more than distant lots.
- Value: completed capacity divided by completed lot area, normalized against the current citywide cap.
- Exclusions: ignore the candidate's consumed built capacity when scoring its replacement, or it may self-justify every redevelopment.
- Decay: poor access, low desirability, or sustained under-occupancy should pull the score down faster than density pulls it up.

This creates a natural downtown effect. Dense lots make adjacent lots more eligible for density, but only when the area also has land value, access, and enough desirability for the relevant RCI type.

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
3. Compute local intensity from land value, access, nearby density, service-site value, and edge/crowding penalties.
4. Convert citywide cap to local max density through land value and demand pressure, then require desirability `60` for growth.
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

- `City Builder/Data/RCI/rci_tools.xml` defines current RCI growth rules and population-density anchors through one million population.
- `City Builder/SimulationRuntime::rciLocalMaxDensityPerTile` applies the local land-value multiplier while evaluating RCI candidates.
- `City Builder/SimulationRuntime::findRciConstructorLotAsset` constrains selected constructor assets by demand, front direction, and density capacity limit.
- `City Builder/SimulationRuntime::rciMaxDensityPerTile` interpolates max density per tile from city population.
- `City Builder/SimulationRuntime::rciDesirabilityForCandidate` is the current binary access hook and the natural place to begin richer local intensity work.
- `City Builder/Tile.h` provides the displayed land-value cap used by the local density multiplier.
- `City Builder/RciTool.cpp:471` and `City Builder/RciTool.cpp:502` are the current parcel/block partitioning paths that should preserve the rural-to-narrow-lot progression.
- `City Builder/RciLotConstructionTests.cpp` covers RCI XML density interpolation expectations, template coverage, primary-module density metadata, and front-only residential access.

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
- The densest low-density template for a footprint should remain below the least dense medium template for that footprint, and the same ordering should hold from medium to high.
- Mature cities should produce dense centers near accumulated value and access, not a uniform map-wide spread of the highest unlocked form.
- A new empty area in a mature region should not instantly become a downtown unless local value and access already justify it.
- Repeated zoning should produce a mixed district with old lower-density fabric, not a single current-best asset everywhere.
