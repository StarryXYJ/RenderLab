# Ray tracing foundation

The `RayTracing` scene composition uses the Deferred GBuffer and an independent Ray Query shadow
feature. Select `--pipeline raytracing`, or **Ray Tracing** in the native debug panel on a supported
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
