# Deferred Pipeline

The scene renderer offers a Deferred composition alongside Forward. The native debug panel lists
only implemented pipelines; its typed choice table is the extension point for future compositions.
The [Ray Tracing composition](raytracing.md) extends this GBuffer with Ray Query visibility.
Use `--pipeline deferred` for a headless run or choose **Deferred** from the scene debug panel.
Forward remains the default and is selected with `--pipeline forward`.

## GBuffer contract

The `Deferred.GBuffer` feature writes three same-size `RGBA32_FLOAT` color attachments and the
shared reversed-Z `CameraDepth` attachment:

| Resource | Channels | Space |
| --- | --- | --- |
| `GBuffer.AlbedoMetallic` | base color RGB, metallic A | linear RGB, scalar metallic |
| `GBuffer.NormalRoughness` | world normal XYZ, perceptual roughness A | world space; Lit roughness in `[0, 1]`, Unlit marker `-1` |
| `GBuffer.EmissionOcclusion` | emission RGB, occlusion A | linear RGB, scalar occlusion |

`Lit.shader` and `Unlit.shader` provide a `GBuffer` pass role. Lit forward and GBuffer passes share
the same vertex transform, material texture sampling, tangent-frame normal mapping and alpha clip.
Opaque and masked materials write the GBuffer; transparent materials remain in the Forward
transparent pass after deferred lighting. An opaque custom Shader without a GBuffer role fails
with the Shader name in the diagnostic instead of silently disappearing.

Unoccupied samples are identified from reversed-Z depth `0`. The color values cleared into those
GBuffer locations are ignored. `CameraDepth` is sampled through a depth-only view; the lighting
pass reconstructs world position with the inverse view-projection matrix, then evaluates the same
direct BRDF, shadow visibility, image-based lighting and emission used by the Forward material.
The Unlit marker returns the stored base color directly and preserves its Forward output.

## Composition and limits

The pipeline order is `Shadow` (when enabled), `Deferred.GBuffer`, `Deferred.Lighting`, then
`Forward.Transparent`. Render Graph declarations connect the three GBuffer textures and sampled
depth to lighting. The RHI supports up to eight color attachments; GBuffer uses three. No
multisample resolve, material ID buffer, decals, tiled lights, or path-tracing integration is part
of this implementation.

The GBuffer currently stores three full-resolution 32-bit float targets. This favors a direct,
inspectable reference over bandwidth efficiency. Compare Forward and Deferred on the same scene
and resolution before drawing performance conclusions; Deferred trades an extra geometry pass and
GBuffer bandwidth for shared screen-space lighting work.

## Run

```powershell
cmake --build --preset windows-debug --target renderlab_sandbox
out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --headless --validation on `
  --pipeline deferred --project sample/m4/Pbr.rlproject --width 640 --height 360
out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --headless --validation on `
  --pipeline deferred --project sample/m4/Pbr.rlproject --width 640 --height 360
```

The standard `--debug-panel` shows the Pipeline choice at the top of the window. Changes apply to
the next scene frame. For detailed controls and validation entry points see [Build and Run](../manual/build.md#deferred-pipeline).
