# Land Value Equilibrium And Density Gating Plan

Use this guide when changing land value, park effect, service desirability, pollution penalties, commute penalties, or the local density cap that turns land value into allowed RCI density.

## Intent

- Make land value a stable place-quality signal rather than a one-off lot bonus.
- Let every completed RCI lot add some positive urban value to its own site. Service lots should add more, because they are meant to create attractive anchors.
- Let overcrowding reduce value through externalities: air pollution now, then noise, water pollution, crime, education gaps, healthcare gaps, utility stress, and similar channels later.
- Make high density emerge only from a rare combination of accumulated nearby development, strong service coverage, useful access, tolerable pollution, and short commutes.
- Keep far-away neighborhoods mostly low density even if they have a nice park, because they lack enough surrounding urban intensity and access.
- Let a crowded downtown still intensify when the player replaces some floor area with a park or future mass-transit station, because the service/access gain can offset lost built capacity.

## Research Takeaways

Urban economics gives us the right qualitative model:

- Bid-rent theory ties land value to accessibility and transport cost. Central, accessible locations can support more intensive land use; peripheral locations trade accessibility for space.
- Hedonic property-value studies treat value as a bundle of location, structure, and neighborhood attributes. That maps neatly to a game score made of access, nearby development, services, and externalities.
- Parks and green interventions generally raise nearby property values, but the effect depends on park type, neighborhood density, and distance. Simple linear or fixed-radius falloff is too crude.
- Transit access can increase value through accessibility, but local nuisance effects can offset it. Future transit stations should add access/service value while their traffic/noise/congestion effects remain separate.
- SC4-style desirability is a useful gameplay reference: parks and services improve desirability, while pollution, traffic noise, and bad commutes suppress growth. Demand is not enough without local desirability.

Sources checked:

- Bid rent theory: https://en.wikipedia.org/wiki/Bid_rent_theory
- Greenspace distance decay in hedonic pricing: https://www.sciencedirect.com/science/article/pii/S2212041621001522
- Green intervention property-price meta-analysis: https://www.sciencedirect.com/science/article/pii/S1462901119310457
- Transit accessibility hedonic study: https://www.ugpti.org/resources/reports/downloads/dp-191.pdf
- SimCity 4 desirability and commute references: https://gamefaqs.gamespot.com/pc/561176-simcity-4/faqs/21501

These are design references, not formulas to copy. The important lesson is the shape of the system: value is local, spatial, and made of both amenities and costs.

## Current Implementation Audit

Current code anchors:

- `City Builder/SimulationRuntime.cpp` computes neighbor diffusion, clamps land value to the displayed min/cap range, and decays air pollution toward zero.
- `City Builder/Lot.cpp` applies lot air-pollution and land-value effects to occupied lot tiles.
- `City Builder/SimulationRuntime.cpp` converts average candidate land value into the current local density factor.
- `City Builder/Tile.h` defines the displayed land-value cap.
- `City Builder/Data/RCI/rci_tools.xml` defines `baselineLandValue`, currently `0`, used to seed new tiles.

The current diffusion pass uses this shape for interior tiles:

```text
newValue = oldValue + sum(neighborValue - oldValue) / 16
```

For a tile with four neighbors this is 75 percent self-retention and 25 percent four-neighbor average. The local pass clamps land value to the display min/cap range and decays air pollution independently. Lots currently apply module air-pollution, park-effect, and land-value effects to their occupied tiles as separate fields. Pollution and park effect no longer subtract from or add to land value directly, and road medians no longer add direct land value.

Current module land-value XML is active again:

- Residential modules add direct land value, scaled roughly by authored density/form.
- Industrial modules add air pollution but currently use zero direct land-value effect.
- Park modules reduce air pollution and emit `parkEffect` but currently use zero direct land-value effect.
- Service, yard, path, parking, and related module land-value effects are neutral or small direct sources until the future composed target-value model replaces this interim source model.

This restores the older direct source behavior while leaving room for a later composed target model.

The important balancing problem is that the current local density formula can double-count density. Population raises the zone-owned citywide density cap. That allows denser residential assets. Those assets emit larger land-value values. The higher land value is then multiplied back into future candidate density through `rciLocalMaxDensityPerTile`. The loop is bounded by the displayed cap and by constructor attempts, so it is not an unbounded one-tick exponential, but it can still create a compounding redevelopment cascade.

## Scratch Simulation

I ran a small local scratch script matching the older coupled equations on a `41x41` grid for `500` ticks. Values below are approximate steady values at the source center and at eastward radii.

| Scenario | Center | r2 | r4 | r8 | r12 | Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| one park source `+10000` | 27,509 | 13,004 | 8,548 | 4,459 | 2,456 | 27,509 |
| one house source `+450` | 931 | 285 | 105 | 0 | 0 | 931 |
| one tower source `+1267` | 3,030 | 1,196 | 645 | 182 | 18 | 3,030 |
| suburban houses grid | 11,059 | 10,639 | 9,329 | 5,132 | 2,927 | 11,059 |
| dense core plus park | 76,610 | 60,751 | 52,132 | 33,066 | 20,657 | 76,610 |
| dense core plus park plus stack | 1,237 | 0 | 0 | 0 | 0 | 1,237 |

This says two useful things:

- The current displayed cap of `160,000` is effectively unreachable through ordinary current lot effects; it mostly comes from initial random seeding or extreme manual pollution/land-value edits.
- Pollution is too crushing when injected as a persistent high source. A polluted downtown should be harmed, but not reduced to near-zero value if it also has density and services.

## Design Model

Land value and desirability should be related, but not the same field.

Land value is the slow urban-intensity cap: accumulated development, useful access, service sites, and nearby density make a place capable of supporting higher-value forms. Park effect, air pollution, and later noise, education, healthcare, or water pollution should remain separate named tile fields. Each RCI type reads those fields through its own desirability tables.

```text
landValueTarget =
    baseUrbanValue
  + developedLotValue
  + serviceSiteValue
  + accessValue
  + nearbyDensitySynergy
  - crowdingValueDrag
```

Then the simulation can move current land value toward that target through damped diffusion:

```text
diffused = self * (1 - diffusionRate) + neighborAverage * diffusionRate
next = approach(diffused, landValueTarget, targetBlend) - globalDecay
```

The key change from the old coupled model is the self-retention term and the absence of direct universal park/pollution modifiers. A starting value around `diffusionRate = 0.15` to `0.25` is easier to tune than the old effective interior rate of `1.0`.

Density-linked sources should be normalized against city maturity before they feed the target. In particular, nearby density should use completed capacity per tile divided by the current zone cap, not raw completed capacity alone. Replacement scoring should exclude the source lots the candidate would consume, otherwise a dense existing lot self-justifies its own next redevelopment.

The system should stay data-driven. Add a future XML file such as:

```text
City Builder/Data/Simulation/land_value.xml
```

It should own static balancing numbers: diffusion rate, target blend, decay, source weights, cap bands, and any future direct land-value channels.

## Lot Effects

Every completed RCI module should have non-negative land-value output. That does not mean every lot is desirable overall.

Recommended split:

- Residential modules: positive land value based on density class and coverage.
- Industrial modules: small positive developed-lot value, plus air-pollution output; whether that pollution matters depends on the RCI desirability table reading it.
- Park/service modules: positive service-site value, plus separate `parkEffect` or air-pollution cleanup fields where appropriate.
- Parking/loading/service yards: low positive or neutral developed-site value, with nuisance handled through future noise/traffic channels instead of negative land value.
- Under-construction lots: no active value contribution until complete, matching current parameter behavior.

This keeps the meaning clean:

- `landValue` is the slow cap for what the place can physically and economically support.
- `parkEffect`, `airPollution`, future `noise`, `education`, `healthcare`, and `waterPollution` are separate conditions.
- `desirability` is how each RCI type reacts to those conditions.

## Cap Bands

The displayed cap remains `160,000`, but it should mean "Manhattan-level conditions", not merely "near a park".

Target bands:

| Band | Land value | Density multiplier | Typical place |
| --- | ---: | ---: | --- |
| Very low | `0-20k` | `10-21%` | rural fringe, weak-access edge, unserviced land |
| Low | `20k-45k` | `21-35%` | ordinary low-density neighborhoods |
| Moderate | `45k-80k` | `35-55%` | serviced suburbs, compact town centers |
| High | `80k-120k` | `55-78%` | mature walkup and midrise districts |
| Very high | `120k-155k` | `78-97%` | strong downtown, dense mixed services |
| Cap | `155k-160k` | `97-100%` | Manhattan-like high density with plentiful services and easy commutes |

The current interim density formula uses land value as the local density factor, with a small starter floor so zero-value land can still grow low-density starter buildings:

```text
localMaxDensityPerTile =
    max(starterDensityFloor, cityMaxDensityPerTile * clamp01(landValue / displayedCap))
```

The balancing work should focus on making land value itself hard to maximize without a genuinely intense and well-served district.

Recommended replacement shape:

```text
localMaxDensityPerTile =
    starterDensityFloor
  + (cityMaxDensityPerTile - starterDensityFloor)
    * smoothstep(localIntensity01)
```

`localIntensity01` should be a saturated score, not a second raw density scale. A geometric mean is a good first prototype because it rewards places that are simultaneously developed, accessible, serviced, and near other completed density, while one missing prerequisite drags the result down:

```text
nearbyDensity01 = completedNearbyCapacityPerTile / max(cityMaxDensityPerTile, starterDensityFloor)
localIntensity01 = geometricMean(developedSite01, access01, nearbyDensity01, serviceSite01) * (1 - edgePenalty01)
```

This keeps population as the only global unlock curve. Local intensity decides how much of that unlock a place can use, rather than multiplying a density-derived field back into another density-derived cap.

## Desirability And Demand Pressure

The cheap first-pass model should avoid speculative pathfinding for hypothetical buildings. Land value already provides the slow local cap, and actual commute assignment already tells us when existing lots have exceeded the current network/job arrangement. Use that feedback instead of asking every candidate to pathfind as if it were already built.

Separate three decisions:

1. **Growth eligibility:** no new RCI growth below desirability `60`.
2. **Maximum density for construction:** desirability does not multiply the cap. If the candidate is eligible, the density cap is local intensity plus modest demand pressure.
3. **Current active capacity for built lots:** built lots recalculate average desirability from the tiles they own and scale active residents/jobs between `50%` and `100%`.

Construction density should use:

```text
effectiveMaxDensity =
    min(
        populationMaxDensity,
        starterDensityFloor
      + (populationMaxDensity - starterDensityFloor)
        * localIntensityFactor
        * demandPressureDensityFactor)
```

Interpretation:

- `populationMaxDensity` is the existing citywide maturity curve by zone.
- `localIntensityFactor` is the local cap: `0.00` at weak local intensity, `1.00` only for an exceptional completed core, with starter low-density growth preserved by a separate floor.
- `demandPressureDensityFactor` lets pent-up demand justify somewhat denser buildings than a relaxed market would choose, but it should have a small slope and a hard cap.
- `desirability >= 60` is a prerequisite for growth, not a partial density multiplier.

Built-lot active capacity should use the lot's own averaged desirability for its RCI type:

```text
currentCapacityFactor =
    clamp(0.5 + 0.5 * ((averageDesirability - 40) / 20), 0.5, 1.0)
```

That means a built lot at desirability `40` runs at `50%` active capacity, desirability `50` runs at `75%`, and desirability `60` or above runs at `100%`. Below `40`, the first pass should keep the `50%` floor rather than immediately abandoning the building. A later abandonment layer can be added as a sustained low-desirability state, but it is not part of the first rule.

The demand a lot satisfies or reserves remains `100%` of its maximum capacity. The number of residents, workers, jobs, pollution outputs, service consumers, and commute trips it actually generates is proportional to current active capacity.

Desirability should be a `0-100` score per RCI type and wealth/job class:

```text
tileDesirability =
    baselineDesirability
  + interpolate(parkEffectTable, normalizedParkEffect)
  + interpolate(airPollutionTable, normalizedAirPollution)
  + interpolate(actualCommuteTable, commuteMetric)
  + futureNamedFieldDeltas
```

Each built lot averages that score across the tiles it owns. Candidate growth should evaluate the candidate footprint the same way, but it should use current fields only, not hypothetical traffic or pollution from the building it might become.

Sensitivity tables should live in RCI XML, either inside `Data/RCI/rci_tools.xml` for the first pass or in a later `Data/RCI/desirability.xml` if the table set grows. Every table must have at least a minimum and maximum row, and important curves should add enough intermediate rows to make interpolation smooth.

Example low-wealth residential pollution and commute curves:

```xml
<rciDesirability rciType="low_wealth_residential" baseline="60">
  <sensitivity field="airPollution" input="normalized">
    <point value="0.0" desirabilityDelta="5" />
    <point value="0.2" desirabilityDelta="0" />
    <point value="1.0" desirabilityDelta="-30" />
  </sensitivity>
  <sensitivity field="actualCommuteTime" input="normalizedMaxCommute">
    <point value="0.0" desirabilityDelta="5" />
    <point value="0.5" desirabilityDelta="0" />
    <point value="1.0" desirabilityDelta="-20" />
  </sensitivity>
</rciDesirability>
```

For the current `600` second maximum commute, that residential commute table means `0` seconds gives `+5`, `300` seconds gives `0`, and `600` seconds gives `-20`. It should read actual accepted commute results for the built lot, not hypothetical future routes for a candidate.

Residential park effect should be a positive curve. Dirty industry should initially declare zero deltas for both `parkEffect` and `airPollution`, keeping it at the baseline `60` for those fields:

```xml
<rciDesirability rciType="dirty_industry" baseline="60">
  <sensitivity field="parkEffect" input="normalized">
    <point value="0.0" desirabilityDelta="0" />
    <point value="1.0" desirabilityDelta="0" />
  </sensitivity>
  <sensitivity field="airPollution" input="normalized">
    <point value="0.0" desirabilityDelta="0" />
    <point value="1.0" desirabilityDelta="0" />
  </sensitivity>
</rciDesirability>
```

Demand pressure should be calculated per RCI type and eventually per wealth/job class. A simple first pass:

```text
demandPressureRatio = unsatisfiedDemand / max(activeCapacityForThisRciType, seedCapacity, 1)
demandPressureDensityFactor = clamp(1.0 + demandPressureRatio * demandSlope, 1.0, demandPressureCap)
```

With `activeCapacity = 1000`, `unsatisfiedDemand = 200`, and `demandSlope = 0.5`, the factor is `1.10`. That matches the desired intuition: a city with demand for 200 more houses against an existing base of 1000 residents can tolerate a small density premium without demand becoming a hidden second population curve. For dirty industry, the denominator should be active dirty-industry capacity/jobs rather than total city population.

The first `demandPressureCap` should be conservative, likely `1.25` to `1.50`, and should be XML-authored. Demand pressure should also be smoothed over time with a short moving average so one tick of demand noise does not cause a sudden tower jump.

This lets a far suburb behave naturally:

- A few nearby services and short local commutes can lift desirability to `60`, allowing some growth.
- Land value still limits the asset density, so the suburb gets houses and occasional midrise apartments before towers.
- Those new apartments add real capacity, traffic, pollution, and commute demand.
- If highways saturate or job access remains shallow, actual commute desirability and active capacity fall, new growth stops below `60`, and skyscraper growth stalls without hypothetical pathfinding.

Commercial and industrial can read the same fields differently later:

- Residential desirability should be highly sensitive to actual commute results, pollution, noise, and park/service coverage.
- Commercial desirability should treat nearby traffic and future transit footfall as positives up to a congestion/noise limit.
- Dirty industry should be insensitive to park effect and air pollution at first, but can later care about worker access and freight congestion.

This gives us RCI-specific behavior without requiring one pathfinding query per candidate asset.

## Scenario Targets

These target values should guide scripts and integration tests.

| Scenario | Expected stable value | Expected growth result |
| --- | ---: | --- |
| Empty rural edge, no services | `0-15k` | only cheapest low-density forms |
| Far suburb with park | `25k-55k` | still low density; occasional compact/duplex at high population |
| Mature suburb with many homes and parks | `45k-75k` | compact lots and rowhouse-like forms, not towers |
| Small town center with jobs/services | `70k-100k` | walkups and small apartments in mature cities |
| Midrise downtown with pollution controlled | `100k-135k` | midrises and some compact towers when citywide cap allows |
| Overcrowded downtown, long commutes | `70k-110k` | density stalls or downgrades until services/access improve |
| Downtown after replacing a midrise with park/station | `120k-150k` | surrounding lots can upgrade despite one lower-capacity service lot |
| Manhattan-like core with dense RCI and services | `155k-160k` | full local density cap |

## Implementation Plan

1. Add a design-only land-value/desirability model script before changing runtime code.
   - Use a small grid, authored source layers, damped diffusion, and CSV/Markdown output.
   - Include scenarios from the table above.
   - Compare current equations against proposed land-value, desirability, and demand-pressure parameters.

2. Export land-value and desirability constants to XML.
   - Add `Data/Simulation/land_value.xml`.
   - Load diffusion rate, target blend, global decay, baseline, source weights, displayed cap thresholds, and demand-pressure cap.
   - Add RCI desirability sensitivity tables under `Data/RCI/rci_tools.xml` or a later `Data/RCI/desirability.xml`.
   - Load the no-growth threshold `60`, the active-capacity floor `0.50`, and the full-active threshold `60`.
   - Validate that every sensitivity table has sorted points, minimum and maximum rows, and known field names.
   - Keep defaults in C++ only as loader fallback values.

3. Split positive lot value from externality penalties.
   - Retune module XML so all RCI primary modules have non-negative land-value effects.
   - Move dirty-industry harm into air pollution, not direct negative land value.
   - Keep park as the first service channel; reserve schema room for education, healthcare, noise, water pollution, and station access.

4. Replace current diffusion with damped diffusion.
   - Start with `diffusionRate = 0.20`.
   - Keep a small decay so under-occupied or unserviced areas drift down.
   - Use stable integer math or fixed-point helpers so parallel chunk passes remain deterministic.

5. Add cheap desirability scoring and active-capacity scaling.
   - Use actual commute assignment results, not speculative candidate pathfinding.
   - Compute per-RCI desirability from XML sensitivity tables over current tile fields such as park effect, air pollution, actual commute results, and future named services.
   - Reject new RCI growth below desirability `60`.
   - Every tick, each built RCI lot averages desirability for its RCI type over owned tiles.
   - Scale active residents/jobs from `50%` at desirability `40` to `100%` at `60`, clamped at both ends.
   - Keep demand reservation at `100%` of maximum lot capacity while active workers/jobs/trips/output use current active capacity.

6. Add per-RCI demand-pressure density scaling.
   - Replace fixed overbuild intuition with `unsatisfiedDemand / activeCapacityForThisRciType`.
   - Apply the resulting multiplier both to construction budget and to maximum density, with a conservative slope.
   - Smooth and cap the multiplier through XML.

7. Feed services into land value.
   - Service lots can add direct service-site value, but `parkEffect` itself stays a separate desirability field.
   - Future services should add named fields, not hardcoded one-off land-value edits.
   - Service-site value and emitted service fields should have distance decay and saturation so repeated identical services have diminishing returns.

8. Preserve the current local density cap interface.
   - `rciLocalMaxDensityPerTile` can continue reading average candidate land value if `landValue` has been converted into the composed local-intensity field.
   - Tests should assert that a far serviced suburb remains below medium/high caps while a dense, serviced, low-pollution core can approach the cap.

## Test Plan

Prefer sandbox integration tests over helper-only unit tests.

- Add a land-value equilibrium test target or extend `RciLotConstructionTests`.
- Use sandbox city layouts: rural edge, serviced suburb, polluted industrial edge, dense downtown, dense downtown with park insertion.
- Run enough ticks to reach stable or near-stable values.
- Assert ranges instead of exact values.
- Assert land-value ordering: rural < suburb < town center < downtown < Manhattan core.
- Assert desirability ordering: polluted residential core < clean residential core, long-commute residential core < short-commute residential core.
- Assert service recovery: downtown with a park/station replacement raises nearby land value and/or RCI desirability enough to unlock higher density.
- Assert cap rarity: no single service lot by itself can push a low-density fringe candidate near the display cap.
- Assert growth gating: new RCI construction does not happen below desirability `60`.
- Assert active-capacity scaling: built RCI at desirability `40` contributes `50%`, desirability `50` contributes `75%`, and desirability `60` contributes `100%`.
- Assert demand reservation: an under-occupied built lot still reserves or satisfies demand as `100%` of maximum capacity while workers/jobs/trips are scaled by active capacity.
- Assert sensitivity interpolation: low-wealth residential air pollution gives about `+5` at `0%`, `0` at `20%`, and about `-30` at `100%` normalized pollution.
- Assert residential commute interpolation: `0%` of max commute gives about `+5`, `50%` gives `0`, and `100%` gives about `-20`.
- Assert dirty-industry neutrality: dirty industry park-effect and air-pollution tables both interpolate to `0`, leaving baseline desirability `60` for those fields.
- Assert demand pressure: with active capacity `1000`, unsatisfied demand `200`, and slope `0.5`, the density pressure multiplier is `1.10` before XML cap/smoothing.

## Open Balancing Questions

- Should industrial land value be one shared value, or should each RCI type eventually read land value differently?
- Should the displayed land-value cap stay `160,000`, or should the number be XML-authored alongside overlay color bands?
- Should residential commute desirability be based on worst route, average route, unsatisfied demand, or a weighted blend?
- Should future mass-transit stations add land value directly, improve the actual commute metric, emit a transit-access desirability field, or combine those?
- Should high density itself add a small positive agglomeration value even before service coverage, or should that be entirely represented by nearby-density synergy?
- Should sustained very low desirability eventually create abandonment, or is active under-occupancy enough for the first few balancing passes?

## Related Guides

- `docs/design/lot-density-progression.md` owns the local density cap and RCI density progression.
- `docs/design/lots.md` owns lot/module effects and construction behavior.
- `docs/design/xml-assets.md` owns asset XML validation and future static-number XML schemas.
- `docs/design/transport-network.md` owns commute time, access, congestion, and future mass-transit hooks.
- `docs/design/city-morphology-art-direction.md` owns the visual meaning of low, medium, and high-density districts.
