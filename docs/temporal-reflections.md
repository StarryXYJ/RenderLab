# Reflection reconstruction and temporal reprojection

The default display denoiser supports static opaque scenes and camera movement. It uses a
reflection-specific reconstruction path; it is an original bounded implementation informed by
[AMD FidelityFX Reflection Denoiser](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/#amd-fidelityfx-reflection-denoiser),
[SVGF](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/) and
[NRD input requirements](https://github.com/NVIDIA-RTX/NRD#noisy-inputs). No third-party denoiser
code or SDK is integrated. Design decisions and the rejected input-clamping experiment are in
[the design review](../design/design-review.md#运动反射白点闪烁设计复查).

## Composition and controls

Three branches consume the same traced sample:

- Reference: `Trace -> progressive Accumulate`. This remains the unfiltered 1/N estimator.
- Uncertainty: `Trace + previous progressive -> Statistics`. This keeps a matching centered moment
  and stabilized reconstruction control, independent of the radiance mean.
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

Filter1 reconstructs a demodulated progressive signal using the temporal radiance as a bounded
prior. Its initial weight is `max(control, instantaneousNoise) * historyLength /
(historyLength + max(0, spp-1))`. A disagreement penalty further reduces the prior when its RGB
differs from the progressive signal beyond the estimated uncertainty. It uses
`support = (spp-1)/(historyLength+spp)` and
`uncertainty = varianceOfMean + tolerance²/max(.01,1-control)`; divide the initial weight by
`1 + support * squaredRgbDifference/(3*uncertainty)`. At one spp the penalty is zero and the
temporal signal is used; additional stationary support and lower measured uncertainty reduce
the prior continuously. These signals share
samples, so this is a reconstruction heuristic, not an independent-estimator precision average.
There is no fixed 8..64 spp handover or extra spatial kernel introduced after stopping.

All three scales use the same reconstruction chain. The first scale responds promptly to high
local uncertainty with `saturate(4 * max(control, instantaneousNoise))`; strides 2 and 4 use
`control^2` and `control^4`. This retires broad filtering before fine filtering. The normal/plane/
roughness checks remain. The first scale uses temporal luminance to guide reflection edges and
a Gaussian `exp(-.25*r²)`; later scales use their current signal and `exp(-.5*r²)`.
Luminance differences use a variance-driven kernel. Variance travels in scratch alpha after
Filter1, with squared normalized weights and the actual center blend coefficient. Because
prepared/temporal/spatial signals overlap, each scale retains a heuristic floor of .25 times its
input variance; this is not a proven covariance bound. Scratch alpha is no longer history length
after Filter1. The persistent temporal alpha remains accepted history length.

Resolve restores the center material factor and retains
`strength = smoothstep(.05,.15,roughness) * saturate(1-spp/2048)` only as the mirror/high-spp display
envelope. Per-pixel uncertainty controls the filtering within that envelope. Resolve no longer
performs a second 5x5 gather. The original standalone spatial baseline is unchanged.

At 2048 spp or above the graph selects the exact progressive RGBA without reconstruction passes;
Resolve retains the matching shader guard for direct contract tests. Sharp mirrors (roughness <=
.05), background and Unlit also return the exact reference at resolve. Neither the stationary spatial
result nor progressive means enter recursive temporal radiance or moments. Temporal statistics
continue to consume independent single-frame inputs. Resuming camera motion selects the motion
path again and resets stationary spp; temporal reuse still follows its validity and high-spp
bypass rules (a history bypassed at >=2048 spp starts cold on the next moving frame). The recursive
history
weight has no spp fade; fading happens only in display resolve. There is no hard input-radiance
cap: an evaluated neighborhood cap suppressed real secondary highlights and was removed.
The prior static checkpoint report is retained at
[stationary display](performance/m5-stationary-reflections.md); the adaptive reconstruction and
whole-sequence footprint measurements are in [adaptive filtering](performance/m5-adaptive-reflections.md).

## Progressive uncertainty

Every progressive history slot has a tightly packed `float2` structured buffer, eight bytes per
pixel: centered demodulated luminance `M2/N`, and stabilized filter control in [0,1]. The first
field uses Welford's recurrence on the original single-frame sample. The previous mean is derived
from the matching unfiltered RGB and the stationary material factor. Its estimated mean variance
is `(M2/N) / max(1,N-1)`. Float32 roundoff remains; the scrambled Sobol sequence is correlated,
so this is an uncertainty proxy, not a calibrated confidence interval or a convergence proof.

Only the control target borrows a geometry-checked 3x3 mean of previous centered moments. This
avoids trusting a quiet pixel before nearby rare bright samples have been observed. The first
eight samples additionally bootstrap from current spatial sample variance. The stored M2 remains
unfiltered. With `tolerance = .1 + .2 * meanLuminance`, the target is
`varianceOfMean / (varianceOfMean + tolerance²)`. Control starts at one and changes by
`clamp(.125*(target-previous), -.04, .02)` per sample/frame. Fine-scale response can react to the
instantaneous estimate; broad-scale weights use the stabilized control to avoid abrupt expansion.

Statistics always update while reflections run, including with display denoising off, so toggles
do not leave missing samples in the progressive moment. Raw/no-history modes reset the moment
with the running mean. Pose/scene/light/material/size/View invalidation follows the matching
progressive history. At the 2^24 sample limit both mean and moment are held. Statistics and their
index/initialization/completion share progressive ownership and publish only after submission.
Buffer initialization is tracked separately from texture initialization: an equal-pixel-count
resize can retain the statistics buffer while replacing the dimensioned radiance texture.

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
The first scale uses these moments to estimate uncertainty in the temporal prior, combined with
the progressive uncertainty. Later scales consume propagated scratch variance. The fast
luminance and spatial-variance channels remain temporal statistics; the current spatial weights
use the uncertainty and control described above. Short histories bootstrap a conservative
luminance kernel. Spatial outputs do not feed the radiance/moment history, avoiding recursive
broadening. This is a bounded variance-guided filter, not a full REBLUR/RELAX/SVGF port.

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
The same diagnostic option also exposes `SceneReadback::reflectionStatistics`: a tightly packed
float2 per pixel, with the two fields described above (no texture row pitch). Wait for completion
before mapping. This extra readback is absent from normal UI frames.

## Memory and remaining limitations

Eight persistent RGBA32F targets cost 128 bytes/pixel: 28.125 MiB at 640x360, 253.125 MiB at 1080p
or 450 MiB at 1440p, excluding alignment and in-flight replacements. This is 32 bytes/pixel more
than the prior temporal implementation. Prepare, three filter scales and resolve alternate two
explicitly versioned full-resolution scratch images. Graph WAR/RAW/WAW dependencies protect each
overwrite; no stage reads an old scratch version after reuse. Full-size diagnostics are optional.
The original standalone spatial target is omitted when this path runs. Temporal targets remain
uncompressed and there is no general memory aliasing; explicit host allocation limits remain.

These eight targets are retained only while reconstruction is active. After a successful frame
without temporal reconstruction (including the stationary 2048-spp bypass), CPU ownership and
initialization flags are released; in-flight commands still retain their allocations. Re-enabling
reconstruction or resuming motion after that bypass starts cold, as before. At 2048 spp or above,
Prepare/Temporal/all three filters/Resolve are omitted, and the denoised texture name directly
aliases the unfiltered progressive texture. The temporal source counter is zero on those frames.
When reconstruction is inactive there is no temporal diagnostic readback.

The transient trace-distance guide is R32F instead of RGBA32F, saving 12 bytes/pixel per allocation
without changing the float32 value. A guide-free raygen variant omits it entirely for unfiltered
views or disabled/bypassed filtering. The active temporal target formats are unchanged; this does
not reduce the precision of HDR moments, world positions or the progressive reference.

Each cached progressive View owns two additional eight-byte/pixel structured buffers, a combined
16 bytes/pixel: 3.516 MiB at 640x360, 31.641 MiB at 1080p or 56.25 MiB at 1440p. They follow the
existing up-to-four progressive View cache and completion-protected slot reuse. A single zeroed
eight-byte buffer supplies the unread previous binding on reset. No new RGBA32F history target
is required. The extra Statistics compute pass runs even when display filtering is off.

Primary disocclusion checks cannot identify every reflected disocclusion. Curved/normal-mapped
surfaces, rapid viewpoint changes and tiny secondary highlights can still blur, trail or shimmer.
The input remains one specular sample per pixel, without adaptive sampling or a radiance cache.
Finite-history filtering is biased and does not prove integrator convergence. Whole-sequence
error, residual changes, signed brightness error and stop recovery must be inspected together;
a low flicker score alone can reward a stale or dark image. Current measurements and hardware
limits are recorded in [the stability report](performance/m5-reflection-stability.md).
