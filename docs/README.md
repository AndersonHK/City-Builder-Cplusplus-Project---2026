# Documentation index

Reviewed September 9, 2026. Current guides describe the implementation; open plans describe intended work. Archived audits retain their original observations and may refer to code or measurements that have since changed.

## Current implementation guides

- [Transport network](design/transport-network.md): lanes, directed routing, traffic and query contracts.
- [Routing implementation record](design/transport-routing-implementation-plan.md): delivered compact routing, deterministic workers, validation and deferred experiments.
- [Real-city routing benchmark](design/transport-routing-real-city-benchmark.md): acceptance measurements for routing/publication commit `79e0319`, using city `(1,1)`.
- [Renderer](design/renderer.md): current OpenGL rendering, immutable snapshots, timed route ribbons, preview depth, and graphics checks.
- [Simulation threading](design/simulation-threading.md): worker ownership, deterministic reduction and publication.
- [Region and saves](design/region-save.md): city lifecycle, save formats and previews.
- [Lots and modules](design/lots.md): occupancy, construction and module effects.
- [Lot density progression](design/lot-density-progression.md): density rules, templates and balancing.
- [Metric art standard](design/metric-art-standard.md): the approved 6 m tile scale.
- [Visual asset pipeline](design/visual-asset-pipeline.md): asset authoring, cooking and the editor.
- [XML assets](design/xml-assets.md): gameplay and UI data contracts.
- [App config](design/app-config.md): presentation/input settings and defaults.
- [Window system](design/window-system.md): query windows, menus and UI layout.

## Design direction and open plans

- [City morphology art direction](design/city-morphology-art-direction.md).
- [Land-value equilibrium plan](design/land-value-equilibrium-plan.md).
- [Vulkan/HDR migration plan](design/vulkan-migration-plan.md): foundations exist; the game still renders with OpenGL.

## Archived proposals and audits

- [Road lane-cell refactor handout](archive/road-lane-cell-refactor-handout.md): May 2026 proposal; current rules are in the transport guide.
- [Routing scalability plan](archive/transport-routing-scalability-plan.md): earlier roadmap and historical checkpoints.
- [Routing architecture assessment](archive/transport-routing-architecture-assessment.md): pre-implementation audit and architecture research. Useful context for future experiments, not a description of the shipped engine.

Use real saves for performance acceptance. Synthetic fixtures remain useful for correctness and algorithm experiments; archived timings and historical benchmark results should not be presented as measurements of a later checkout.
