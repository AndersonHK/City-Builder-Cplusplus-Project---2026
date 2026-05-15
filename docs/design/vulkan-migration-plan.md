# Vulkan Migration Plan

This plan covers a full migration from the current OpenGL renderer to Vulkan. The API name is spelled Vulkan; any "Vulcan" references in planning should be treated as this migration.

## Local Findings

- The simulation side is already in a good shape for Vulkan. `SimulationRuntime` publishes immutable `PublishedWorldSnapshot` data, and rendering acquires/releases a pinned snapshot instead of reading mutable simulation buffers.
- The OpenGL blast radius is concentrated in `City Builder/Renderer.cpp`, `City Builder/ShaderProgram.*`, `City Builder/Basic.shader`, and `City Builder/City Builder.vcxproj`.
- `Renderer.cpp` currently owns too many concerns at once: GLFW window setup, OpenGL object lifetime, camera math, picking, culling, upload freshness, procedural road atlas generation, region preview textures, draw ordering, and metrics.
- The current renderer has seven shader modes:
  - `0`: tile pass with tile-state color, lot lift, and ground-road texture overlays.
  - `1`: committed and ghost lot prisms.
  - `2`: elevated roads and road ghost previews.
  - `3`: tile overlays such as traffic capacity.
  - `4`: queried commute route arrows.
  - `5`: region city previews.
  - `6`: screen-space in-game UI window quads.
- Current persistent textures map cleanly to Vulkan images:
  - tile state: full-map `GL_RG16_SNORM` -> `VK_FORMAT_R16G16_SNORM`.
  - lot lift: full-map `GL_R8` -> `VK_FORMAT_R8_UNORM`.
  - ground roads: full-map `GL_RGBA8` -> `VK_FORMAT_R8G8B8A8_UNORM`.
  - tile overlays: full-map `GL_RGBA8` -> `VK_FORMAT_R8G8B8A8_UNORM`.
  - road atlases and region previews: RGBA8 sampled images.
- The renderer's visible-dirty chunk upload model should survive the migration. Vulkan should replace `glTexSubImage2D`/`glBufferData` with explicit staging uploads and image/buffer copies, not change the simulation handoff contract.
- `City Builder/City Builder.vcxproj` still depends on local legacy OpenGL paths and copies `glew32.dll`/`glfw3.dll`. That should be cleaned up as part of the renderer migration rather than dragged forward.

## External References Checked

- [GLFW Vulkan guide](https://www.glfw.org/docs/latest/vulkan_guide.html): required instance extensions, `GLFW_NO_API`, presentation support, and `glfwCreateWindowSurface`.
- [Khronos Vulkan Tutorial introduction](https://docs.vulkan.org/tutorial/latest/00_Introduction.html): current tutorial baseline uses Vulkan 1.4, dynamic rendering, timeline semaphores, and SPIR-V-oriented shader tooling.
- [Khronos dynamic rendering guide](https://github.khronos.org/Vulkan-Site/tutorial/latest/courses/18_Ray_tracing/01_Dynamic_rendering.html): use `vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR` instead of old render pass/framebuffer setup.
- [Khronos mapping data to shaders](https://docs.vulkan.org/guide/latest/mapping_data_to_shaders.html): vertex input, descriptors, uniform/storage buffers, samplers, and push constants.
- [Khronos synchronization guide](https://docs.vulkan.org/guide/latest/synchronization.html): Vulkan makes synchronization application-owned and validation-assisted.
- [Khronos memory allocation guide](https://docs.vulkan.org/guide/latest/memory_allocation.html): favor suballocation; Vulkan memory management is nontrivial.
- [LunarG Vulkan SDK](https://www.lunarg.com/home/vulkan-sdk/): SDK provides loader, validation layers, shader tools, capture/replay, and debugging tools.
- [Vulkan Memory Allocator](https://gpuopen.com/vulkan-memory-allocator/): practical single-header allocator for Vulkan buffers/images.

## Migration Path

### Phase 0: Freeze The Render Contract

Document the renderer-facing data contract before changing code:

- `PublishedWorldSnapshot` fields and lifetimes.
- chunk revision rules for tile state, lift, ground roads, elevated roads, and overlays.
- draw order and depth/blend state.
- current status-line renderer metrics.

Acceptance check: a short update to `docs/design/renderer.md` or this plan that names the render payloads and expected freshness semantics.

### Phase 1: Split Renderer Ownership Without Changing Behavior

Before introducing Vulkan, split `Renderer.cpp` into smaller units so Vulkan code does not land inside a 2,600-line mixed OpenGL file.

Recommended slices:

- `RenderMath.*`: `Vec2`, `Vec3`, `Vec4`, `Mat4`, frustum, projection, picking.
- `RenderInstances.*`: `TileInstanceData`, `LotInstanceData`, `RoadInstanceData`, `RouteArrowInstanceData`, `UiQuadInstanceData`, builders.
- `RoadAtlas.*`: CPU road atlas painting.
- `RendererOpenGL.*`: current OpenGL backend.
- `Renderer.*`: high-level frame loop, snapshot acquisition, controller hookup, metrics.

Keep OpenGL running in this phase. This reduces risk and creates obvious files for a future `RendererVulkan.*`.

### Phase 2: Add Vulkan Build And Tooling

Add Vulkan support beside the existing OpenGL build:

- Install the LunarG Vulkan SDK on the dev machine.
- Add project macros for `$(VULKAN_SDK)\Include` and `$(VULKAN_SDK)\Lib`.
- Link `vulkan-1.lib`.
- Keep GLFW, but remove GLEW only after Vulkan reaches parity.
- Add `volk` for function loading if desired.
- Add VMA for allocation/suballocation.
- Add shader build steps that compile source shaders to SPIR-V beside the executable.
- Split `Basic.shader` into explicit shader stage files, or replace it with Slang/HLSL/GLSL sources that compile to `.spv`.

Recommended first target: a compile-only Vulkan backend that creates/destroys a Vulkan instance under validation layers.

### Phase 3: Bring Up Vulkan Window, Device, Swapchain

Replace the OpenGL context path for the Vulkan backend:

- Set `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`.
- Query `glfwGetRequiredInstanceExtensions`.
- Create `VkInstance` with debug utils in debug builds.
- Create a `VkSurfaceKHR` through `glfwCreateWindowSurface`.
- Select a physical device with graphics and present support.
- Create logical device and queues.
- Create swapchain, swapchain image views, and a depth image.
- Use current framebuffer size from GLFW, and recreate the swapchain on resize/fullscreen changes.

Acceptance check: the app opens a Vulkan window, clears to the current background color, resizes cleanly, and exits without validation errors.

### Phase 4: Port Shaders And Pipeline Layouts

Port the current shader behavior into Vulkan-compatible shader modules:

- Replace global uniforms with push constants or per-frame uniform buffers.
- Use descriptor sets for sampled images and samplers.
- Keep vertex attribute locations compatible with current instance structs.
- Prefer one pipeline per render class instead of one large `uRenderMode` branch long term:
  - tile/ground-road pipeline.
  - lot pipeline.
  - elevated/ghost road pipeline.
  - overlay pipeline with depth disabled.
  - route-arrow pipeline with depth disabled.
  - region-preview pipeline.
- Start with the same visual math and colors. Do not redesign art during the API migration.

Important Vulkan-specific adjustment: current projection math is OpenGL-style. Vulkan clip depth is `0..1`, and framebuffer Y handling differs from OpenGL. Fix this either in the projection matrix or by using the Vulkan viewport convention intentionally, then verify picking and visible chunk culling still match.

### Phase 5: Port Static Geometry And Instance Buffers

Create Vulkan buffers for:

- static tile quad vertices.
- static box vertices.
- persistent per-chunk tile instances.
- per-chunk elevated road instances.
- dynamic road ghost instances.
- dynamic lot instances and lot ghosts.
- dynamic query route arrow instances.
- dynamic in-game UI window quad instances.
- one region preview instance buffer.

Use VMA-backed buffers and staging uploads. For dynamic payloads, use a per-frame upload ring so `glBufferData(..., GL_DYNAMIC_DRAW)` behavior becomes explicit and does not stall on GPU use.

Acceptance check: tile chunks draw in Vulkan with a flat placeholder color before texture sampling is enabled.

### Phase 6: Port Texture Resources And Chunk Uploads

Create Vulkan images, image views, and samplers for each current texture.

Upload strategy:

- tile state and lift: continue packing visible chunks into CPU scratch vectors, then stage-copy to the full-map image.
- ground road and overlay textures: either copy from source rows using `VkBufferImageCopy::bufferRowLength = snapshot.width`, or pack chunk rows into a contiguous scratch buffer before upload.
- road atlases: preserve the CPU atlas generator and upload once.
- region previews: upload on `previewRevision` changes.

Every image upload needs explicit layout transitions, normally:

- shader read -> transfer dst.
- copy buffer to image.
- transfer dst -> shader read.

Batch chunk copies per frame where possible. Keep hidden stale chunks stale, exactly as OpenGL does now.

### Phase 7: Port Draw Order And Render State

Record command buffers in the existing order:

1. tiles with ground roads.
2. elevated roads.
3. road ghosts with alpha and depth writes disabled.
4. lot ghosts with alpha and depth writes disabled.
5. committed lots.
6. tile overlays with depth test disabled.
7. query route arrows with depth test disabled.
8. in-game UI windows with depth test disabled and screen-space projection.

Use dynamic rendering. Keep depth test `less-or-equal` behavior for world geometry and preserve alpha blending for ghosts, overlays, and route arrows.

Acceptance check: controls and visual checks from `docs/design/renderer.md` pass at `32`, `64`, `128`, `256`, `512`, `1024`, and `2048` visible-tile zoom.

### Phase 8: Integrate Frames In Flight With Snapshot Lifetimes

Use two frames in flight at first. Three is possible later, but two keeps latency and lifetime reasoning easier.

The simulation snapshot should be released after the renderer has copied/packed all CPU data needed for recorded commands. The GPU should never directly read `PublishedWorldSnapshot` CPU memory. If a future optimization maps snapshot-owned memory directly into Vulkan buffers, snapshot release must wait for GPU completion or use a new ownership contract.

Keep `fastForward` behavior intact. If the renderer falls behind, Vulkan frames in flight must not let the simulation overwrite a buffer that CPU upload work still depends on.

### Phase 9: Region Mode Parity

Port region previews after city rendering is stable:

- map each `City::previewPixels()` revision to a Vulkan image.
- use the existing region camera and picking math.
- draw one textured quad per city.
- ensure entering city mode invalidates upload freshness, as the region-save guide requires.

Acceptance check: launch to region, double-click into a city, save, exit to region, and confirm the preview updates.

### Phase 10: Remove OpenGL

After Vulkan parity:

- delete `ShaderProgram.*`.
- remove `#include <GL/glew.h>` and all `gl*` usage.
- remove `opengl32.lib`, `glew32.lib`, `glew32.dll`, and OpenGL shader runtime compilation.
- keep GLFW only for window/input/surface creation.
- update `README.md`, `docs/design/renderer.md`, and project filters.

Acceptance check: project builds and runs without OpenGL or GLEW installed.

## Potential Challenges

- Vulkan is much more explicit. Synchronization, image layouts, swapchain ownership, and buffer lifetimes replace OpenGL's hidden driver behavior.
- Shader migration is not a find-and-replace. Descriptor sets, push constants, SPIR-V compilation, clip-space conventions, and pipeline layouts all need deliberate design.
- The current renderer uses `glTexSubImage2D` with row-length state for partial full-map uploads. Vulkan can do this, but the copy regions and buffer row lengths must be exact.
- Swapchain recreation must be robust across window resize and `Alt+Enter` fullscreen toggles.
- Validation noise will be high early. Treat validation errors as migration blockers, not cosmetic warnings.
- The current Visual Studio project has legacy absolute dependency paths. Vulkan migration should clean dependency setup instead of adding another local-only path.
- Region previews are large `4096x4096` RGBA textures. Vulkan memory and upload scheduling need to avoid accidental per-frame churn.
- If both OpenGL and Vulkan backends temporarily coexist, shared renderer data must stay backend-neutral. Avoid letting OpenGL naming leak into new contracts.
- The simulation can outrun rendering. Frames-in-flight and snapshot release must be audited carefully so CPU-side upload packing never reads a released buffer.

## Expected Benefits

- Better long-term fit for large-map rendering, richer terrain, richer buildings, and future 3D presentation.
- Explicit control over uploads should make the existing visible-dirty chunk strategy easier to profile and optimize.
- Command buffers and frame-local resources give a cleaner path to multi-threaded command recording later.
- Vulkan tooling gives stronger validation, capture/replay, and GPU debugging than the current OpenGL path.
- Removing runtime GLSL compilation makes startup and deployment more deterministic.
- The renderer can evolve toward modern features such as compute-built overlays, indirect draws, descriptor indexing, and eventually more serious material/mesh systems.
- The migration preserves the project's main doctrine: simulation remains tile-first and authoritative; rendering stays a presentation layer over published snapshots.

## Recommended First Implementation Slice

The safest first slice is not Vulkan code. First split `Renderer.cpp` into backend-neutral math/data helpers and the current OpenGL backend, then update `docs/design/renderer.md` to name those seams. After that, add a Vulkan backend that only opens a clear-color window. That sequence gives every later Vulkan port a place to live and keeps the current game playable while parity is built.

## Validation Checklist

- Build `x64 Release`.
- Build and run `TransportNetworkTests.vcxproj`.
- Launch with validation layers enabled.
- Confirm no Vulkan validation errors during startup, resize, city mode, region mode, and shutdown.
- Check zoom levels `32`, `64`, `128`, `256`, and `512`.
- Pan after road and lot edits to confirm hidden stale chunks upload when visible.
- Drag ground roads and elevated highways; confirm ghost previews match OpenGL behavior.
- Place and rotate each lot ghost; confirm committed lots match previews.
- Toggle `T`; confirm traffic overlay draws above roads/lots.
- Query a lot; confirm route arrows draw above overlays.
- Query a lot; confirm the UI window draws above route arrows and uses the XML/fallback layout rules.
- Save to region and return; confirm region preview texture updates.
