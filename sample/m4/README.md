# M4 scene sample

Original cube/sphere geometry and checker texture. Copper uses a project shader referencing the engine HLSL library. The second directional light is intentionally present; the active main light is selected by stable object UUID. The hidden cube exercises CPU frustum culling.

Pbr.rlproject selects the M4.3 PBR fixture. The upper row uses metallic copper at perceptual roughness 0.08, 0.3, 0.65 and 1; the lower row uses dielectrics (the last two also exercise normal/MR maps). A small lower sphere uses Unlit HDR. Studio.png, PbrNormal.png and PbrMetallicRoughness.png are original procedural test data. Camera, environment intensity/rotation and exposure are fixed in Pbr.scene; edit and restart to compare settings.

Run from the repository root:

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --project sample/m4/Pbr.rlproject
```

Use `--backend vulkan` for the other backend. See [build instructions](../../manual/build.md#m43-pbr-参考场景) and [PBR contracts/limitations](../../docs/m4-forward-pbr.md).
