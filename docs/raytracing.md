# Ray tracing foundation

The `RayTracing` scene composition uses the Deferred GBuffer and an independent Ray Query shadow
feature, followed by independent TraceRays specular reflections. Select `--pipeline raytracing`, or **Ray Tracing** in the native debug panel on a supported
device. Forward and Deferred remain available as raster references. Path tracing and Forward+ are
separate planned compositions and are not exposed as placeholder menu entries.

## Capabilities and RHI

`Runtime::SupportsAccelerationStructures`, `SupportsRayQuery` and `SupportsRayTracingPipeline`
report the optional features enabled by the selected device. Vulkan enables the corresponding
KHR extensions/features and buffer device addresses only when supported; a request for an
unavailable feature fails with a capability diagnostic. There is no implicit rendering fallback.
The Ray Query program targets Shader Model 6.5 / Vulkan 1.3 SPIR-V.

`QueryAccelerationStructureSizes` queries native result, build scratch and update scratch sizes.
`CreateAccelerationStructure` allocates a BLAS/TLAS object with fixed build description, input
storage and scratch. Native updates require `allowUpdate` and a preserving
`ReadWriteBuffer(..., AccelerationStructureBuildWrite)` declaration; the graph retains the original
build producer. Owners that update later must keep or reacquire scratch. The scene renderer uses
new result storage for changed instances. The initial geometry subset is indexed or unindexed opaque triangles, float3 positions
with an explicit stride/offset, and optional uint32 indices. TLAS transforms are row-major 3x4
affine matrices with 24-bit instance IDs and 8-bit masks. Dynamic vertex/skin evaluation and AS
compaction are not implemented.

Buffers used for geometry/instances have explicit `deviceAddress` and
`accelerationStructureInput` flags. AS result storage has `accelerationStructureStorage` and
cannot be used as an ordinary shader/copy buffer. DX12 creates it in the permanent native
`RAYTRACING_ACCELERATION_STRUCTURE` state; logical build/read transitions emit the required UAV
memory dependency. Vulkan uses its AS build and traversal access masks. Scratch alignment is
included in the Vulkan allocation requirement and the selected address is aligned to the
device's scratch limit.

## Graph declarations and ownership

AS storage and scratch remain explicit graph buffer resources. The operation-specific uses are:

| Use | Declaration | Meaning |
| --- | --- | --- |
| `AccelerationStructureBuildInput` | read | Vertex, index or packed instance input |
| `AccelerationStructureBuildWrite` | write | AS result or scratch |
| `AccelerationStructureRead` | read | Traversal, or BLAS consumed by a TLAS build |

`ComputeContext::BuildAccelerationStructure` verifies the declared result, scratch, input buffers
and referenced BLAS storage before recording a native build. `BindAccelerationStructure`
verifies that its declared storage belongs to the bound TLAS. Bindings and recorded commands
retain AS versions, geometry and instance data. Build packets also retain scratch directly, so
the owner can release its scratch reference after a successful submission without freeing it
before GPU completion. Failed graph compilation/recording does not publish a scene AS version.

## Scene updates and shadows

`AccelerationScene` caches one BLAS per static mesh. Its first build uses a small graph and
restores the mesh buffers to Vertex/Index states. A TLAS is rebuilt into new storage when instance
membership, transforms, IDs, masks or the referenced BLAS change. Unchanged frames and camera-only
changes reuse the existing TLAS. This versioned rebuild policy favors predictable lifetime over
in-place refitting; old bindings keep earlier versions alive while frames remain in flight.

The shadow ray mask is bit 0; bit 1 reserves the same opaque scene for surface-ray effects.
Material ShadowCaster policy controls bit 0. Transparent geometry continues through the Forward
transparent pass and does not enter the AS. Masked materials currently produce an explicit
unsupported-material diagnostic because alpha candidate evaluation is not implemented.
The reference `sample/m4/RayTracing.rlproject` shares the existing asset directory and contains
only an opaque floor, sphere and cube; it can switch among all three implemented compositions.

`RayTracing.Shadows` reads `CameraDepth`, world-space GBuffer normals, main-light parameters and
the TLAS. It writes an `RGBA8_UNORM` `RayTracing.ShadowVisibility` texture before Deferred Lighting.
Only direct directional lighting is attenuated. Background pixels and a disabled shadow setting
produce visibility 1; the Shadow texture view displays this mask. The current rays use a 0.002
world-unit normal bias, 0.001 minimum distance and 10000 maximum distance. Rays treat opaque
triangles as two-sided blockers. Transparent lighting continues to use the raster shadow map.

## Validation entry points

`--as-test` exercises native size queries, graph BLAS/TLAS builds and updates, actual Ray Query hit/miss,
instance add/move/remove, an empty TLAS and scratch release after submission. The Ray Tracing
scene acceptance checks image changes, camera reuse, TLAS rebuild conditions, an empty scene and
two in-flight scene versions. Run commands and hardware results are recorded in
[Build and Run](../manual/build.md) and [the phase plan](../design/phaseplan.md).

## Ray Tracing Pipeline and ShaderTable

A ray pipeline provides exactly one RayGeneration, Miss and generic triangle ClosestHit module.
`PipelineDesc` explicitly declares payload bytes (default 16), triangle attributes (8) and maximum
recursion depth (1 or 2). DXC compiles individual DXIL `lib_6_5` exports or SPIR-V ray entry points.
Pipeline cache identity includes all modules and ray configuration. This bounded API does not yet
support callable, any-hit, procedural geometry, local root arguments or per-material hit programs.

`Runtime::CreateShaderTable` creates immutable raygen/miss/hit records and retains both pipeline
and addressable upload buffer. DX12 uses 64-byte table spacing (32-byte shader identifiers);
Vulkan queries handle size, handle alignment, base alignment and maximum stride, including an
aligned offset within the buffer. Native handles/identifiers are copied from the actual PSO.
The upload bytes must remain immutable. A changed shader generation requires a new PSO and a
new table; never reuse identifiers from an old pipeline. Publishing a replacement at a frame
boundary leaves old command packets owning their previous table, PSO and resources until retirement.
Built-in raw ray shaders are compiled on renderer creation; automatic file watching is not provided.

`Graph::AddRayTracingPass` exposes a typed `RayTracingContext`. Declare the table buffer with
`Use::ShaderTableRead`, the TLAS with `AccelerationStructureRead`, and every shading buffer/texture
with its shader read use. `TraceRays` checks the table storage declaration; RHI checks it belongs
to the bound pipeline. Dispatch retains the table even if its caller releases the last reference.
Vulkan shader resource barriers cover ray stages as well as raster and Compute.

`RaySurface.hlsli` and `ray_surface.hpp` define an explicit indexed data ABI: shared 32-byte mesh
vertices, uint32 indices, 16-byte mesh/instance records and 80-byte material records. TLAS InstanceID
selects an instance, which selects mesh and material; PrimitiveIndex selects the triangle. One
hit group interpolates normals/UVs and evaluates the shared metallic-roughness BRDF. A finite 8x8
tile atlas holds 64x64 maps with clamped one-texel borders. Color maps are linearized before packing;
base color, metallic/roughness, normal, occlusion and emission are sampled at explicit LOD 0.
There are no raster derivatives or implicit mip selection. This deliberately bounded representation
is independent of material asset count and does not require M6 descriptor arrays.

`--as-test` also executes real TraceRays hit/miss cases, indexed PBR evaluation, explicit texture
sampling and two rebuilt PSO/table generations on each backend. Ray Query results do not stand in
for these native pipeline checks.

## Hybrid reflections

Ray Tracing now requires all three enabled capabilities: AS, Ray Query and Ray Tracing Pipeline.
The composition is GBuffer → Ray Query shadows → Deferred Lighting → reflection trace → stationary
accumulation → optional spatial denoise → reflection combine → Forward transparency → display. The Graph exposes separate
`RayTracing.Reflection.Trace`, `.Accumulate`, optional `.Denoise` and `.Combine` timings and dependencies.

Trace reads Depth, world Normal/Roughness, Albedo/Metallic, TLAS and indexed geometry/material
buffers. One visible-normal GGX (VNDF) importance-sampled specular ray produces RGBA32F `RayTracing.RawRadiance`.
The 16-byte payload holds incoming radiance and hit distance. A separate RGBA32F
`RayTracing.HitGuide` stores current-sample distance in x: positive for a hit, -1 for an
environment miss, zero for invalid/rejected rays. Raw radiance alpha remains the hit indicator;
unfiltered history alpha remains its running mean. ClosestHit evaluates shared PBR,
emission, main directional lighting (with an inline shadow query) and the existing IBL; Miss
samples the environment. Recursion depth is one: secondary surfaces do not launch another
specular bounce. This is a hybrid reflection effect, not a path tracing integrator.

Combine replaces the existing IBL specular contribution with accumulated traced specular light,
weighted by `1 - roughness²` and primary occlusion. Rough surfaces increasingly retain the stable
prefiltered IBL baseline. The diffuse IBL, emission and direct-light terms stay in Deferred Lighting.
The reference is finite-sample GGX reflection with no firefly radiance clamp. The optional
spatial display filter described below never changes the unfiltered reference.
Some noise remains during motion and after resets. Transparent surfaces keep their Forward
lighting and do not participate in these reflection rays.

### Histories and invalidation

Histories are keyed by stable camera ID (ViewID), bounded to four views, with two completion-protected
RGBA32F textures per view. Each submission reads the preceding version and writes the other texture
on the same queue; it publishes the new index/signature only after successful submission. Resize
replaces textures while command references retain old in-flight allocations. The running mean
uses weight `1 / (N + 1)` for every new sample, including beyond 64 frames. The count/sequence do not
wrap early. At 2^24 samples (the exact integer limit of the float parameter ABI), accumulation holds
the completed mean instead of changing to a fixed-weight EMA. There is no moving-camera reprojection
in M5.

The exact input signature includes current view/projection and camera position, target dimensions,
enabled instance membership/world transforms, packed geometry/material generation, texture-atlas
generation, environment identity/ambient/intensity/rotation, directional light values and the shadow
setting. A mismatch sets history count and sample sequence to zero. Returning to an unchanged ViewID
can resume its own history. Switching to raster or another diagnostic view clears retained histories.
Exposure is a display transform and does not invalidate linear radiance.

`SceneRenderOptions::reflections=false` retains the M5.2 shadow-only baseline;
`reflectionHistory=false` forces deterministic sample zero. The panel/CLI expose **Reflection (raw
sample)** / `--view reflection-raw` and **Reflection (history)** / `--view reflection-history`.
Raw view always uses sample zero and skips prior history, providing an unfiltered reference.
Forward and Deferred remain the pure raster references. Unsupported material selections are
preflighted in the window host and restore the last working pipeline with a concrete diagnostic.

### Spatial denoising

`SceneRenderOptions::reflectionDenoise` defaults to true for Final/HDR. The native panel checkbox
**RT spatial denoise (Final / HDR)** and CLI `--denoise on|off` select the composite input without
resetting the sample count. **Reflection (denoised)** / `--view reflection-denoised` always requests
the filtered diagnostic, independently of the Final checkbox. Raw and History always stay unfiltered.

The independent Compute pass reads accumulated radiance, primary depth/world normals/roughness,
albedo/metallic and the current stochastic hit-distance guide. A bounded 5x5 joint bilateral kernel
uses a projection-derived world-space footprint and a hard primary plane-distance guard, normal,
roughness, material and soft hit-distance weights. It does not compare radiance brightness or clamp
highlights. Alpha is copied unchanged. Background and Unlit pixels bypass filtering. Roughness
<= .05 bypasses the filter to preserve sharp mirrors; strength ramps to full by .15. The display
blend is `smoothstep(.05,.15,roughness) * saturate(1-spp/2048) / (1+spp/64)` and is exactly zero at
2048 spp and above. Thus the high-spp output returns to the unfiltered estimator.

The filtered texture is transient and **never written back into either persistent history**.
Switching Final/History/Denoised or toggling denoising preserves a valid history (Raw still resets).
The hit guide is also transient: it is a noisy sample, not stable reflected depth or a reprojection
buffer. Rough reflections may still blur small reflected details; hit-distance weighting is a
heuristic and low-spp filtering introduces spatial bias. Primary depth/normal boundaries do not
fully identify reflected material or lighting boundaries. The 2-pixel radius cannot remove all
low-spp noise. No motion vectors, temporal reprojection, variance estimation or temporal denoiser
are implemented; camera movement still starts accumulation over. Only stochastic reflection is
filtered, not the Deferred direct-light term or the hard Ray Query shadow mask.

The current format baseline adds two full-resolution RGBA32F transients (guide and filtered output):
32 bytes/pixel, about 7.03 MiB at 640x360 or 28.13 MiB at 1280x720, excluding allocator alignment.
With the filter off only the guide remains; Trace writes it along with raw radiance. High-spp
filter bypass still dispatches a copy pass. Compression and guide-free trace variants are deferred.
See [measurements and limits](performance/m5-denoise.md) for hardware evidence.

### Material and sampling boundary

Reflected surfaces support built-in Lit and Unlit opaque static meshes. Custom opaque shaders
must provide an explicit compatible surface implementation before reflection support; having a
GBuffer role alone does not define their hit shading. Masked geometry is rejected until any-hit
alpha evaluation exists. Meshes remain indexed uint32 triangles with 32-byte vertices. ShaderTable
size stays three records regardless of instance/material count.

The atlas uses 64 tiles, including white and flat-normal defaults, and supports up to 62 distinct
image/color-space/file-version combinations during a renderer session. A version change allocates
a new tile; exceeding that bound produces a diagnostic (restart or reduce textures). Images are
resampled to 64×64, color maps are decoded from sRGB, UVs and borders clamp to match the current
point-clamp sampler, and rays sample explicit LOD 0. Normal frames derive from triangle edges and
UVs, with inverse-transpose interpolated normals. No texture arrays, native mip chains, anisotropic
footprints or bindless resource indexing are introduced.

### Acceptance and costs

`--scene-test --pipeline raytracing` retains the shadow-only checks and additionally exercises real
reflection tracing, separate GPU pass timings, deterministic raw output, stationary accumulation,
camera/scene/material/size/light invalidation, ViewID isolation, an empty scene and consecutive
in-flight history versions. It writes reference BMPs, graph JSON and `reflection-gpu-cost.txt`.
These per-pass Debug/validation timings are diagnostic GPU costs, not a Release performance claim.
Use the benchmark runner with validation disabled and record the build configuration for a
performance comparison; include Forward or Deferred and the same scene/resolution. The initial
Release measurements and raster/reflection images are in [the M5 cost record](performance/m5-reflections.md).

## Stationary convergence correction (2026-10-27)

The initial M5 implementation changed to a 1/64 EMA after 64 frames. It therefore retained a
nonzero noise floor even when left still. That behavior is corrected to a progressive arithmetic
mean. The reflection sampler now uses a digitally shifted 2D Sobol sequence and visible-normal GGX
sampling (Heitz 2018). With the matching PDF, the non-Fresnel reflection throughput is `G2/G1(V)`,
bounded by one; the old NDF sampler could generate large weights at grazing angles. No light or
exposure reduction, firefly radiance clamp or denoiser is used to obtain the comparison.

The debug panel and timing report expose the last completed frame's actual reflection sample
count and observed history resets. In Final/history view, the count should keep increasing while
still; Raw view intentionally stays at sample zero. Camera/scene/material/light changes still reset
history. The sampling contract runs six view-angle/roughness cases on GPU (4096 samples each),
checks finite bounded weights and quadrant stratification, and checks the real accumulation shader
at reset, 1/128, 1/2048 and the maximum count. Scene acceptance includes 130 static frames and zero
camera input after real movement. Image convergence evidence is recorded in
[the correction record](performance/m5-convergence.md).
