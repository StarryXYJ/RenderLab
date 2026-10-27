# Reflection reconstruction and temporal reprojection

The default display denoiser supports static opaque scenes and camera movement. It uses a
reflection-specific reconstruction path; it is an original bounded implementation informed by
[AMD FidelityFX Reflection Denoiser](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/#amd-fidelityfx-reflection-denoiser),
[SVGF](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/) and
[NRD input requirements](https://github.com/NVIDIA-RTX/NRD#noisy-inputs). No third-party denoiser
code or SDK is integrated. Design decisions and the rejected input-clamping experiment are in
[the design review](../design/design-review.md#运动反射白点闪烁设计复查).

## Composition and controls

Two branches consume the same traced sample:

- Reference: `Trace -> progressive Accumulate`. This remains the unfiltered 1/N estimator.
- Display: `Trace -> Prepare -> Temporal -> Filter1 -> Filter2 -> Filter3 -> Resolve -> Combine`.

The display branch never feeds the progressive mean, and its moments never count progressive means
as independent samples. `reflectionDenoise` / `--denoise on|off` is the master display switch.
`reflectionTemporal` / `--temporal-denoise on|off` selects the reconstruction path or the original
standalone 5x5 spatial baseline. Both default on. The panel retains **RT denoise (Final / HDR)** and
**Temporal reprojection (requires denoise)**. Denoised view displays the resolved result; Raw and
History are always unfiltered. `reflectionHistory=false` fixes sample zero and disables temporal
reuse. Toggles preserve a compatible progressive mean; disabling temporal reuse makes its next
enabled frame cold.

Prepare reconstructs primary world position from depth and removes a bounded material factor
`max(.02, F0 * EnvironmentBrdf.x + EnvironmentBrdf.y)` using the same LUT as IBL. Geometry-checked
3x3 filtering stabilizes the single-frame radiance. Positive in-lobe distances use a weighted
harmonic mean; environment-only neighborhoods use 10000 and invalid neighborhoods use zero.
Hit/miss differences do not gate radiance filtering. A receiver-reprojected distance is smoothed
temporally when both guides are valid. This guide is an approximation, not secondary geometry.

Resolve restores exactly the same material factor. For the first eight stationary spp it retains
the motion reconstruction, blended with the progressive reference using
`strength = smoothstep(.05,.15,roughness) * saturate(1-spp/2048)`.
Between 8 and 64 spp, `smoothstep(8,64,spp)` transitions display to a reconstruction of the growing
progressive mean. At 64 spp and above, temporal RGB no longer supplies display energy. This avoids
limiting a stationary image to a clipped 32-frame history until nearly 2048 spp.

The stationary reconstruction gathers a geometry-checked 5x5 neighborhood of progressive RGB,
demodulates each tap and restores the center material factor. Gaussian weights use
`exp(-.25*(x*x+y*y))`; luminance differences in the filtered temporal image guide the reflection
edges with `exp(-abs(Lq-Lcenter)/max(.05,.25*max(Lq,Lcenter)))`. The guide is used only for spatial
weights, not as the radiance being averaged. It avoids making a noisy sample or random hit/miss
the static filter's edge test. The spatial result is blended with the center progressive mean
using `strength`. This still has finite-radius blur and guide-dependent bias; it cannot preserve
every tiny secondary highlight.

At 2048 spp or above, and for sharp mirrors (roughness <= .05), Resolve returns the exact
progressive RGBA. Background and Unlit also bypass reconstruction. Neither the stationary spatial
result nor progressive means enter recursive temporal radiance or moments. Temporal statistics
continue to consume independent single-frame inputs. Resuming camera motion selects the motion
path again and resets stationary spp; temporal reuse still follows its validity and high-spp
bypass rules (a history bypassed at >=2048 spp starts cold on the next moving frame). The recursive history
weight has no spp fade; fading happens only in display resolve. There is no hard input-radiance
cap: an evaluated neighborhood cap suppressed real secondary highlights and was removed.
Static checkpoints and stop/resume results are recorded in
[the stationary display report](performance/m5-stationary-reflections.md).

## Reprojection and temporal statistics

Temporal tests two candidate history locations:

1. **Receiver:** current primary world position projected by previous view-projection.
2. **Reflection:** for roughness < .4 and a finite positive stabilized distance, extend the current
   camera ray through the receiver by that distance and project this virtual position. Intersect
   the previous-camera ray to it with the receiver's tangent plane to estimate the previous
   receiver position. This is a planar specular approximation, not a general curved-mirror solution.

Each bilinear tap must have a valid history count, normal dot >= .95, roughness delta <= .04,
world distance <= two current pixel footprints and normal-plane displacement <= .5 footprint.
Clip depth, positive w and screen bounds are checked before sampling. The virtual candidate
additionally needs >= .5 support, compatible stabilized distance and a neighborhood radiance
score comparable to the receiver candidate. Rejected candidates fall back to a valid receiver
candidate or a cold pixel. A random single-frame distance difference never directly attenuates
receiver radiance history.

Accepted bilinear taps are normalized. Effective previous count is reduced for support < .5;
history weight then equals `count/(count+1)` with a maximum stored count of 32. Diagnostics expose
both the weight and count. History RGB is clipped to a geometry-checked 5x5 current mean +/- three
standard deviations (upper bound adds .02 in demodulated units). This limits stale content but can
still bias specular detail. It does not alter raw samples or the progressive mean.

Temporal stores first and second luminance moments, an eight-frame fast luminance estimate and
current spatial variance. New/short histories bootstrap variance from spatial statistics.
Three 5x5 edge-aware filters use strides 1, 2 and 4, with geometry/normal/roughness rejection and a
luminance kernel controlled by temporal variance and effective history length. Short histories
receive wider reconstruction. Spatial outputs do not feed the radiance/moment history, avoiding
recursive broadening. This is a bounded variance-guided filter, not a full REBLUR/RELAX/SVGF port.

## Ownership, invalidation and diagnostics

One active ViewID owns two completion-protected sets of four RGBA32F targets:

| Target | RGB / first components | Alpha / last component |
| --- | --- | --- |
| Radiance | Accumulated demodulated radiance | Effective history length |
| World | Primary world position | Stabilized in-lobe distance |
| Normal | Primary world normal | Perceptual roughness |
| Moments | First moment, second moment, fast luminance | Spatial luminance variance |

Graph declares every read/write and exports all history outputs as Sampled. Matrices, signature,
index and completion are published only after successful submission. Replaced targets stay alive
through in-flight command ownership. Switching active ViewID starts cold; progressive histories
keep their separate up-to-four-view cache.

Scene membership/transforms/material/atlas/environment/light changes, projection or size changes
invalidate the source. Camera translation > one world unit per frame or direction change > 30
degrees is a cut. These fixed thresholds are conservative and scene-scale dependent. There are
no animated-object motion vectors. The sampling sequence continues across camera movement for
compatible scenes while progressive spp still reset on exact pose changes. Raw remains sample
zero; sequence/count retain the existing 2^24 limit.

`SceneRenderOptions::reflectionDiagnostics` optionally copies per-pixel RGBA32F diagnostics into
`SceneReadback::reflectionDiagnostics`: history weight, effective length, path (0 cold, 1 receiver,
2 virtual), stabilized distance. Wait for submission completion before mapping; row pitch is
`TextureRowPitch(width, Rgba32Float)`. The normal UI binds only a zeroed 1x1 diagnostic target and
skips per-pixel diagnostic writes (`Options.z = 0`). It does not request readback. The panel's
`reflectionTemporalFrames` remains CPU-compatible source frames, not accepted pixels or spp.

## Memory and remaining limitations

Eight persistent RGBA32F targets cost 128 bytes/pixel: 28.125 MiB at 640x360, 253.125 MiB at 1080p
or 450 MiB at 1440p, excluding alignment and in-flight replacements. This is 32 bytes/pixel more
than the prior temporal implementation. Prepare, three filter scales and resolve alternate two
explicitly versioned full-resolution scratch images. Graph WAR/RAW/WAW dependencies protect each
overwrite; no stage reads an old scratch version after reuse. Full-size diagnostics are optional.
The original standalone spatial target is omitted when this path runs. No compression or general
memory aliasing was added; explicit host allocation limits remain.

Primary disocclusion checks cannot identify every reflected disocclusion. Curved/normal-mapped
surfaces, rapid viewpoint changes and tiny secondary highlights can still blur, trail or shimmer.
The input remains one specular sample per pixel, without adaptive sampling or a radiance cache.
Finite-history filtering is biased and does not prove integrator convergence. Whole-sequence
error, residual changes, signed brightness error and stop recovery must be inspected together;
a low flicker score alone can reward a stale or dark image. Current measurements and hardware
limits are recorded in [the stability report](performance/m5-reflection-stability.md).
