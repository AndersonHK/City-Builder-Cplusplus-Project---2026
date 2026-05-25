# Vulkan Native HDR Renderer Plan

This repository treats "Vulcan" as Vulkan. The target renderer is Vulkan-only, scene-linear HDR internally, and GPU-resident by default. OpenGL/GLEW are legacy implementation debt and should be removed once the Vulkan backend is buildable with the local SDK/toolchain.

## Current Contract

- Simulation remains authoritative and publishes immutable snapshots.
- Renderer-facing color is `LinearColor` / `HdrColor` float RGBA in scene-linear space.
- Authored XML/module/lot/UI colors convert from sRGB to scene-linear at load or render-boundary time.
- Byte payloads are allowed for masks, glyph IDs, packed road flags, direction/lane flags, save booleans, and similar non-color data.
- Overlay payloads are not CPU-colored RGBA. They are compact typed data:
  - traffic capacity: one configured renderer scalar payload per tile, with one relevance bit plus the remaining utilization bits.
  - zoning: one configured renderer scalar payload containing the zoning semantic ID per tile.
  - land value and RCI desirability: one configured renderer scalar payload per tile, packed against semantic caps such as `kSimulationStatDisplayCap` and `kRciDesirabilityDisplayCap`.
- The current renderer scalar payload storage is 16-bit. `kRendererScalarPayloadBitDepth`, `kRendererScalarPayloadMaxValue`, traffic masks, and signed scalar limits derive from the renderer payload contract instead of hardcoded channel values. Code should read these as renderer semantics, not as magic channel literals.
- Gradient calibration is cap-based and fixed-point: minimum maps to zero, half-cap maps to midpoint, and cap or over-cap maps to full scale. If the land-value cap changes from the current compile-time value, updating `kSimulationStatDisplayCap` should automatically update renderer packing and tests.
- Gradient direction is explicit renderer metadata, not bespoke shader logic. Red means bad and green means good: traffic utilization and air pollution use `GoodToBad`, while land value and RCI desirability use `BadToGood`.
- Shader code owns ramps, alpha, tint policy, exposure, tone mapping, and output encoding.
- Static graphics should live in VRAM: terrain meshes, road atlases, lane graphics, lot meshes/materials, region preview textures, and stable object instances.
- Per-tick upload paths should send only what changed: dirty chunk IDs, tile scalars, road topology deltas, object transforms, construction progress scalars, and revision counters.
- Silent fallbacks that move renderer work to CPU or downgrade the pipeline are not allowed. Missing Vulkan/HDR requirements must be logged and fail that renderer path loudly.

## Vulkan Backend Requirements

- GLFW window creation uses `GLFW_NO_API`.
- Required Vulkan instance extensions come from GLFW; debug utils and validation layers are enabled in debug builds.
- The surface is created with `glfwCreateWindowSurface`.
- Device selection requires graphics and present queues, dynamic rendering support, and the swapchain/color-space support needed by the output mode.
- Memory allocation uses VMA or an equivalent suballocator.
- Uploads use explicit staging buffers, batched image/buffer copies, and explicit layout transitions.
- Render passes use dynamic rendering.
- World and preview depth images use `VK_FORMAT_D32_SFLOAT`; no 24-bit fallback should survive the migration.

## Implemented Foundations

- `VulkanRendererSupport` defines the Vulkan scene color/depth formats, swapchain output preference order, and DXGI monitor capability query helper.
- `RendererPayload` centralizes fixed-point scalar packing, signed tile-state limits, traffic relevance/utilization masks, overlay semantic indices, and overlay gradient directions.
- `RendererTests` covers Vulkan swapchain format selection so FP16 scRGB wins first, HDR10 wins second, and SDR is rejected unless explicitly allowed. It also covers cap-calibrated land value, desirability, and traffic payload endpoints.
- The Visual Studio projects now consume the local Vulkan SDK include/lib paths for x64 builds.

## HDR Composition

- World, roads, overlays, lots, region previews, loading screens, and UI render into an internal `VK_FORMAT_R16G16B16A16_SFLOAT` scene target.
- Final presentation is the only place where exposure, tone mapping, gamut/output encoding, and clamp happen.
- Swapchain output selection order:
  1. `VK_FORMAT_R16G16B16A16_SFLOAT` with `VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT`.
  2. `VK_FORMAT_A2B10G10R10_UNORM_PACK32` with `VK_COLOR_SPACE_HDR10_ST2084_EXT`.
  3. SDR sRGB only when HDR modes are unavailable and the selected run mode explicitly permits SDR.
- Windows monitor capability logging uses DXGI `IDXGIOutput6::GetDesc1`, matched to the monitor containing the window center.
- Startup logs must include DXGI monitor capability, Vulkan device, supported HDR surface formats, selected swapchain format/color space, internal scene format, and depth format.

## Pipeline Split

- Tiles and ground roads.
- Lots and construction progress.
- Elevated and ghost roads.
- Scalar/semantic overlays.
- Query route arrows.
- Region previews.
- Screen-space UI and loading screens.
- Zoning parcel overlays.
- Final HDR presentation pass.

## Removal Checklist

- Delete `ShaderProgram.*`.
- Delete `Basic.shader` once Vulkan shader modules replace it.
- Remove GLEW includes, `glew32.lib`, `glew32.dll` copy steps, and `opengl32.lib`.
- Remove all `gl*` calls and OpenGL project settings.
- Keep GLFW only for input/window/surface creation.
- Remove the OpenGL default-framebuffer 32-bit request plus 24-bit fallback, and the city-preview 24-bit depth fallback.

## Local Tooling Status

- `vulkaninfo` is present and reports Vulkan 1.4 plus `VK_EXT_swapchain_colorspace`.
- Local `vulkaninfo` reports FP16 scRGB and HDR10 10-bit surface-format candidates.
- `VULKAN_SDK`, `glslc`, and `glslangValidator` should come from the installed Vulkan SDK environment; project files must use `$(VULKAN_SDK)` or `PATH` rather than a machine-specific install folder.

## Validation

- Build:
  - `msbuild 'City Builder/City Builder.vcxproj' /p:Configuration=Release /p:Platform=x64 /m`
  - `msbuild 'City Builder/RendererTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m`
  - `msbuild 'City Builder/TransportNetworkTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m`
- Code analysis:
  - `msbuild 'City Builder/City Builder.vcxproj' /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /m`
  - `msbuild 'City Builder/RendererTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /m`
- Run:
  - `Distributable/x64/Release/RendererTests.exe`
  - `Distributable/x64/Release/TransportNetworkTests.exe`
- Manual:
  - HDR on/off launch, resize, Alt+Enter, enter/exit city, refresh region previews.
  - Verify overlays/UI stay readable.
  - Confirm Vulkan validation reports no errors during startup, city mode, region mode, resize, swapchain recreation, and shutdown.
  - Confirm final binary runs without OpenGL/GLEW installed and has no `gl*`, `ShaderProgram`, or `Basic.shader` dependency.
