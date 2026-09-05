# Metric art standard

Approved scale, 2026-09-05: **one tile is 6 metres on each side (36 m²)**.
The basic two-tile, two-way street occupies a 12 m corridor. `WorldScale.h`
defines the conversion. The earlier “half an SC4 tile” comparison refers to
side length and visual parcel grain; it does not override the 6 m decision.

## Road yardstick

The 12 m corridor accommodates two approximately 3.35 m travel lanes,
sidewalks, and curb/gutter/marking margins. The road atlas reserves 1.8 m at
each outer edge for sidewalk and curb presentation. This is an art-scale
decision; simulation speed and capacity tables retain their existing values.

References consulted:

- [FHWA highway geometric characteristics](https://www.fhwa.dot.gov/policy/23cpr/chap4.cfm): typical lane widths vary with road class; 10–12 ft arterial/collector and 9–12 ft local lanes support this order of magnitude.
- [US Access Board PROWAG discussion](https://www.access-board.gov/prowag/preamble.html): 48 in continuous pedestrian clear width and 60 in passing provisions inform sidewalk scale.
- [FHWA pavement marking widths](https://highways.dot.gov/safety/other/visibility/synthesis-pavement-marking-research/chapter-3-pavement-marking-width): normal road markings are inches wide, not metre-wide strips.

## Architecture

All recipes are authored in metres, then normalized only for mesh storage.
Rigid models recover their declared physical dimensions at placement time.
Lot size must not stretch a door, window, floor or parked car.

| Element | Baseline |
| --- | --- |
| Residential floor-to-floor | 3.05 m |
| Entrance leaf | 1.05 × 2.12 m |
| Typical window | 1.25 × 1.45 m |
| Pedestrian path | 1.5 m; 1 m private passages on narrow parcels |
| Private driveway pavement | 3 m wide |
| Driveway composition tile | 6 × 6 m |
| Parked car body | 1.78 × 4.25 m |
| Viewer reference person | 1.75 m |

Floors and gross floor area give capacity a visible architectural counterpart.
Large residential recipes use roughly 28 m² of gross floor area per resident
as an authoring estimate, with tower podium area counted separately. This is
an art heuristic, not a change to the simulation's occupancy model. Industrial
floor heights differ from residential ones. Capacity, pollution and land-value
values remain authored simulation data; changing a visual recipe does not
silently rebalance them.

## Lot access

Vehicle access follows building type. Detached houses, trailers and suburban
duplexes seek a driveway; industrial buildings need yard/loading access.
Dense rowhouses and walkups do not acquire suburban driveways merely because
some grass remains. Tower podiums can have a garage apron.

Long driveways compose `driveway_mid_module` and a
`driveway_cap_left_module`, `driveway_cap_right_module`, or
`driveway_cap_both_module`. Every piece is one
tile square. Only the middle repeats. Caps keep the car at a fixed scale and
provide a side path at the inner end. The planner prefers contiguous space
beside the house and chooses length from the setback, then joins the cap to
the entrance path. Cap depth ends near the front entrance so the connection
does not need to circle the house. A constrained parcel can legitimately omit a vehicle bay.

Pedestrian paving is routed from the front sidewalk to the actual building
entrance zone, around buildings and fixed prop footprints. Each tile has nine
2 m ownership sub-tiles. Access is reserved before random landscaping; trees and
shrubs can coexist with paths in unclaimed cells. The trunk/root area blocks
pedestrians, rather than the overhead canopy. Optional props use nearby free
sub-tiles or are omitted when no valid position remains.
The same reconstruction applies to older saved lots, whose primitive-era
visual stretches and short path stubs are superseded at render time. Access
geometry is presentation data; it does not add residents or occupied tiles.

## Visual references and provenance

The [official SC4 gallery](https://www.ea.com/games/simcity/simcity-4) was used
to compare roof shapes, facade rhythm, planted setbacks and street frontage.
The [Philadelphia Rowhouse Manual](https://www.phila.gov/media/20190521124726/Philadelphia_Rowhouse_Manual.pdf)
adds aerial block photographs and examples of stoops, porches and rear access.
These support the distinction between suburban parking and dense pedestrian
frontage. The models and material swatches in this revision are original
procedural assets; the reference images are not shipped as game textures.

Review at lot scale, not only on an isolated model: a convincing building can
still fail if its approach ends in grass, trees occupy the driveway, or the
parcel layout loses its street frontage.
