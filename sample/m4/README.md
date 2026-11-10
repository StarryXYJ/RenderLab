# M4 scene sample

Original cube/sphere geometry and checker texture. Copper uses a project shader referencing the engine HLSL library. The second directional light is intentionally present; the active main light is selected by stable object UUID. The hidden cube exercises CPU frustum culling.

Pbr.rlproject selects the M4.3 PBR fixture. The upper row uses metallic copper at perceptual roughness 0.08, 0.3, 0.65 and 1; the lower row uses dielectrics (the last two also exercise normal/MR maps). A small lower sphere uses Unlit HDR. Studio.png, PbrNormal.png and PbrMetallicRoughness.png are original procedural test data. Camera, environment intensity/rotation and exposure are fixed in Pbr.scene; edit and restart to compare settings.

Run from the repository root:

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --project sample/m4/Pbr.rlproject
```

Use `--backend vulkan` for the other backend. See [build instructions](../../manual/build.md#m43-pbr-参考场景) and [PBR contracts/limitations](../../docs/m4-forward-pbr.md).

## 阴影与透明

`Shadow.rlproject` 选择 `Shadow.scene`：实体球、带 AlphaGrid 测试纹理的 Masked 方块、透明薄片和接收地面。方向光启用单张 512² 阴影；WASD/QE/方向键与 Pbr 场景相同。运行与限制见 [构建手册](../../manual/build.md#m44-阴影与透明场景)。

Add `--debug-panel` for native view/exposure/shadow controls, or press F1 in a scene window. `renderlab_player.exe --project ...` uses the same content. Fixed captures, quality presets, profiling and standalone installation are described in [M4.5](../../manual/build.md#m45-演示与-player).

## Deferred 与 Ray Tracing

`RayTracing.rlproject` 选择不透明地面、球和立方体参考场景，Pipeline 菜单可切换 Forward、Deferred、Ray Tracing。Ray Tracing 使用 Ray Query 阴影和一次 TraceRays 反射；静止相机逐步累积，运动或场景/材质变化会重置历史。Texture 菜单可查看原始单样本与累积反射。

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --project sample/m4/RayTracing.rlproject --pipeline raytracing --debug-panel
```

需要支持 AS、Ray Query 和 Ray Tracing Pipeline 的设备；Vulkan 使用同一入口。Pbr 项目也支持三种管线。原始 Copper 自定义 Shader 没有 GBuffer/命中表面实现，保持 Forward；Shadow 项目含 Masked 几何，目前不能进入光追模式。窗口切换不兼容管线时显示具体原因并恢复上一选择。Path Tracing 和 Forward+ 暂未实现，后续可扩展现有菜单。接口与限制见 [光追基础](../../docs/raytracing.md)。

## Cornell Box

`CornellBox.rlproject` 是 Cornell Box 风格的室内 GI 示例，复用本目录的立方体和球体网格：红/绿侧墙、白色墙面、漫反射球、粗糙金属球、方块和自发光顶灯。背景环境为黑色，没有方向光；表面照明来自路径追踪命中顶灯后的光传输。它是自主搭建的近似测试场景，不是原始 Cornell Box 的精确几何或光度复现。

在 Editor 使用 File / Open Project 选择该 `.rlproject`，再在 Render Settings / Pipeline 中选择 Path Tracing；也可从仓库根目录直接运行：

```powershell
./out/dotnet/RenderLab.Editor/bin/Release/net10.0/RenderLab.Editor.exe --project sample/m4/CornellBox.rlproject --pipeline pathtracing
```

顶灯为自发光几何，现有路径追踪没有面光源重要性采样，低样本会有明显噪声；静止累积或开启 Spatial denoise 可改善预览，降噪有偏差。当前最多四段路径，且仅支持不透明几何；该示例不代表标准参考图或完全收敛的 GI 基准。普通光栅管线不会模拟该顶灯的间接照明。
