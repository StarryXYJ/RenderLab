# Reflection camera reprojection and temporal denoising

This is a bounded first implementation for static opaque scenes and camera movement. It does not
implement animated geometry motion vectors, virtual reflected-surface motion or a complete SVGF/NRD
pipeline. All primary instance transform/material/light changes invalidate the temporal source.

## Composition and controls

`Trace -> unfiltered progressive Accumulate -> spatial Denoise -> Temporal -> Combine`.
The last two passes are display filters with separate history. They never write back to the
unfiltered 1/N radiance history. Raw and History texture views remain unfiltered.

Final/HDR use `SceneRenderOptions::reflectionDenoise` as the filter master switch and
`reflectionTemporal` for temporal reuse (both default true). The native panel exposes **RT denoise
(Final / HDR)** and **Temporal reprojection (requires denoise)**. CLI `--denoise off` disables both;
`--temporal-denoise off` retains the spatial filter. Denoised diagnostic view requests the spatial
filter independently of the master checkbox and uses temporal filtering when its own switch is on.
Raw and `reflectionHistory=false` fix sample zero and disable temporal reuse. Switching filters
never resets an otherwise valid unfiltered stationary mean. Disabling temporal filtering makes its
next enabled frame cold; disabled histories retain their bounded allocations for reuse.

The report/panel's temporal source frame count measures consecutive compatible CPU source frames,
not accepted GPU pixels, local history length or reflection spp. GPU rejection is per pixel. The
ordinary RT spp counter continues to reset on camera movement, independently of this source count.

## Reprojection

At each valid primary pixel the shader reconstructs world position from current reversed-Z depth
and inverse view-projection, then projects it with the previous view-projection. Negative/zero w,
outside clip depth or UV are rejected. No explicit velocity texture is required for this static
geometry subset. Each of four bilinear taps is validated before interpolation:

- Previous radiance alpha is at least one; previous surface is valid.
- World-space distance is at most two current pixel footprints; displacement along the current
  normal is at most half a footprint. Footprint derives from the actual projection at this depth.
- Normal dot product is at least .95 and roughness difference is at most .04.
- Current/previous stochastic reflection hit distance supplies a soft confidence penalty. It is
  not stable secondary geometry and cannot identify every reflected edge.

Support below .1 rejects reuse. Accepted history is clipped in HDR RGB to the intersection of a
geometry-checked 3x3 current neighborhood's min/max and mean +/- two standard deviations. This
limits stale highlights but can bias or desaturate them. It does not clamp the original trace or
unfiltered history. View-direction disagreement attenuates reuse by
`exp(-64*(1-dot(currentView,previousView))/max(.01,roughness^2))`.

Radiance alpha stores a per-pixel effective history length capped at 16, used for a bounded
`count/(count+1)` blend, multiplied by valid support, view confidence and the same spp fade as the
spatial filter. Sharp mirrors (roughness <= .05), background and Unlit bypass temporal filtering.
At 2048 stationary spp and above the output RGB is exactly the current unfiltered mean; the
history-length channel is zero so a later movement does not revive an obsolete filtered sample.

## Ownership and invalidation

One active ViewID has two completion-protected sets of three RGBA32F textures: filtered radiance /
length, primary world position / stochastic hit distance, and normal / roughness. The unfiltered
mean still has its independent up-to-four-view cache. Switching active ViewID starts temporal
reuse cold. Graph declares every previous read and current write and exports the three outputs
as Sampled. Previous matrices/signature/index/completion are published only after submission
succeeds. Commands retain replaced/old-size textures while in flight.

The temporal signature includes dimensions, projection, geometry/material/atlas versions,
instance membership/transforms, environment and lighting/shadow settings. It excludes camera pose.
A per-frame translation over one world unit or camera direction change over 30 degrees is treated
as a cut. These conservative fixed scene-unit thresholds can reject valid reuse at other scales;
smaller cuts still rely on per-pixel geometry checks and clipping.

The raw running mean retains its exact pose signature and resets on movement. Its independent
Sobol sample index continues across camera movement for a compatible scene instead of repeating
sample zero. Raw/reference mode remains deterministic. The sample sequence has a 2^24 period;
the unfiltered mean still freezes at its exact float count limit of 2^24. Scene/projection changes
restart the sequence. A temporal history length is never used as the unfiltered sample count.

## Memory and limitations

The six persistent RGBA32F targets add 96 bytes/pixel: 21.09 MiB at 640x360, 189.84 MiB at
1920x1080 or 337.50 MiB at 2560x1440, excluding native alignment and old in-flight replacements.
They are additional to GBuffer, unfiltered histories and spatial transients. Scene host allocation
limits remain explicit; this first implementation does not compress guides or alias resources.

Primary reprojection cannot reproduce reflected-object motion under a moving camera. Glossy
reflections and disocclusion can still flicker or trail; low-spp clipping/spatial filtering introduces
bias. There are no object velocities, temporal moment history, variance-guided spatial radius,
adaptive sampling, demodulation or reflected virtual-position reprojection. World/normal checks
and whole-scene invalidation protect the supported static subset, not arbitrary dynamic content.
Other hardware and 4K support require separate validation. Measurements and images are recorded
in [the temporal record](performance/m5-temporal.md).
