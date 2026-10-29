# M4：项目与场景数据

链路为 Project → AssetDatabase → glTF/材质 → Scene → Snapshot/CPU Culling → RenderFrame/DrawPacket → Forward → Graph → RHI。已接入 [Forward PBR、HDR 与 IBL](m4-forward-pbr.md)、[阴影与 Alpha](m4-shadows-alpha.md) 和 [调试工具/Player](m4-tools.md)；动画/蒙皮与编辑器仍在后续阶段。

## 模块与入口

| 模块 | 职责 |
| --- | --- |
| project/project | Manifest、UUID、挂载根、路径边界 |
| assets/database | UUID/路径索引、.meta、依赖/子资源身份、重命名 |
| assets/gltf、image | 静态模型导入、CBOR 缓存、WIC 解码 |
| math/math | CPU 变换、投影、逆矩阵、AABB 与保守视锥判断 |
| scene/scene | 可编辑文档、保存加载、层级变换、主光源与值快照 |
| renderer/scene_renderer | GPU 投影、Material 适配、帧全局数据与现有 Forward 组合 |

启动命令见 [构建与运行](../manual/build.md#m4-项目场景)。sample/m4 的 Cube/Sphere、棋盘纹理与场景均为原创，Copper Shader 演示项目引用引擎 HLSL；原 Custom Graph Demo 保留。

## 项目与稳定身份

RenderLab.rlproject 包含 version=1、项目 id、assets/library 相对目录与 startScene UUID。宿主以 exe 附近 shaders 为引擎挂载根，项目不保存开发机绝对路径。

UUID 使用 32 位小写十六进制；filename.meta 保存 id、dependencies、subresources。Database 扫描 Assets，缺失元数据时生成身份，重复 UUID/非法子资源身份报错。Library 不能与 Assets 重叠或取项目根；路径解析拒绝绝对路径和逃逸，包括根外符号链接。

场景与模型/材质通过 UUID 引用。Rename 同时移动源与 .meta；模型已登记的外部 URI→UUID 依赖会跟随资源移动；Shader entry/include 仍使用文本相对路径，移动后需同步这些声明。首次导入前，模型外部 URI 仍需有效，不会猜测丢失文件。

子资源键使用唯一 mesh/material/node 名称；重名或无名时回退索引，primitive 使用所在 mesh 的索引。UUID 保存在 .meta，普通移动/清缓存/重导入不改变身份。名称或索引变化属于键变化，首版不推断任意拓扑编辑后的对应关系。

## 静态 glTF 与缓存

支持 glTF 2.0 JSON、GLB、外部/嵌入式 Buffer、base64 data URI、交错顶点与 unsigned 8/16/32 位索引。Position/Normal/UV 为 float32；缺法线时由三角形生成，缺 UV 时为零。accessor、offset、stride、索引和有限数值均有校验。

选定 glTF scene 的节点/primitive 实例保留组合变换；无 scene/nodes 的 mesh 资源可按单位变换使用，或通过 MeshRenderer.mesh 选择单个 primitive UUID。

导入 baseColorFactor/baseColorTexture、Opaque 与 doubleSided。M4.3 默认使用 Lit，并导入 Metallic-Roughness、Normal、Occlusion 和 Emission 参数及纹理，缓存格式随之升级；具体参数范围见 [Forward PBR](m4-forward-pbr.md)。采样固定为 RHI 的 point-clamp，显式其他采样方式报错。

glTF 的 OPAQUE、MASK、BLEND 与 alphaCutoff 映射到内置材质角色，缓存版本为 v3；覆盖材质以用户 .material 为准。角色、阴影及透明排序见 [M4.4](m4-shadows-alpha.md)。动画、skin、morph、压缩/required 扩展、稀疏 accessor、非三角形和归一化整数属性明确报错。外部 URI 限普通相对 UTF-8 路径；网络和 percent 编码 URI 不支持。源文件上限 128MiB，图像上限 8192×8192，无 mip/流送。

Library/Models 的 CBOR 缓存键包含 Importer 版本、模型与实际依赖内容。变化重新导入，损坏缓存重建，缓存几何/材质/索引/图像/矩阵也做校验。导入不改写 .material，清 Library 不覆盖用户资产。

WIC 解码复用调用线程已有的 COM apartment，不改变其初始化计数；仅在线程尚未初始化 COM 时创建临时 MTA，并在解码结束或异常退出时配对释放。Sandbox 窗口线程在创建窗口和设备前初始化 STA，保持到这些宿主对象销毁之后；headless 宿主持有 MTA。独立材质纹理可能在交换链建立后的首帧才解码，因此 COM 生命周期由宿主维护，不能随单张图像解码结束而卸载。

## 场景与变换

对象拥有 UUID、parent、enabled、Transform，可包含 Camera、MeshRenderer、DirectionalLight。变换采用 position、xyzw quaternion、scale，拒绝循环、重复 ID、缺失 parent 与奇异变换。

世界为右手、Y-up、相机局部 -Z。CPU 矩阵按行存储、列向量运算；material::Pack 转换到列主序 GPU 布局。透视使用有限远平面与 0..1 Reversed-Z。法线用逆转置并在像素阶段重新归一化；镜像变换通过 PSO 绕序处理，双面材质背面翻转法线。

方向光是带旋转/颜色/强度的场景组件。可有多个；每帧按 UUID 字典序选择首个继承启用状态且 Light 启用的对象，不依赖容器顺序。强度为零仍是选中光；禁用/删除后再选下一盏。光线沿局部 -Z 发射，Shader direction 表示从表面指向光源，取相反方向；父级旋转生效，位置和缩放不影响方向。

无主光时直射强度为零，环境光独立配置。颜色与光照使用线性数值，颜色纹理由 sRGB 解码。M4.3 使用 RGBA32F HDR 中间附件，经曝光与 ACES fitted Tone Mapping 写入 sRGB 显示目标；Scene 同时保存环境图 UUID、强度、旋转与曝光。

## 快照与渲染

Extractor 从 CPU 文档提取相机、主光和实例值快照，延迟回调不再访问文档。CPU 保守剔除变换后的 AABB。instance ID 组合对象/node/primitive，上一帧矩阵按该身份关联，移除后丢弃历史。历史定义为上一次提取结果，未接 TAA/运动矢量。

M5 增加独立于相机和灯光开关的 `allInstances` 表供 AS 构建与射线实例索引使用；`instances` 保持相机可见集，`shadowCasters` 保持启用光栅阴影时的候选集。两种候选集都从同一实例身份和值数据派生。

M6 GPU 剔除模式以 cpuCulling=false 提取，cpuVisibilityKnown=false，此时 instances/culledInstances 不提供 CPU 可见结果；allInstances 与 shadowCasters 仍完整。默认 CPU 路径保留原契约。材质输入在每帧首次读取时冻结；对象提取使用临时 ID 索引，GPU/CPU 剔除与诊断契约见 [GPU Driven](gpu-driven.md)。

MeshRenderer.model 为模型 UUID，mesh 可指定 primitive 子资源 UUID；materials 将导入材质子资源 UUID 映射到用户 .material UUID。GPU Mesh/图像为可重建投影；静态资源准备时上传，常量按帧创建，命令引用/完成点保护在途资源。普通帧不调用 WaitIdle。

View/Lighting/Shadow 常量每帧创建并共享，分别包含相机、主光/环境与阴影数据；Object 保存模型、法线、上一帧模型矩阵，同实例多 Pass 复用。Material 返回 PSO 与绑定值，适配成 Graph 名称/声明，由 Graph 建立绑定组；保留 generation 以复用 ShaderModule/Layout/PSO，不重复准备两份 BindingGroup。

## HLSL 库与项目 Shader

.shader 按需 use View/Object/Scene/Shadow.params 引入带 Schema 的接口；Scene.params 只提供照明。入口 include 生成绑定头，Core.hlsli 通过显式矩阵/相机参数提供：

- TransformObjectToWorld / TransformWorldToView
- TransformObjectToHClip / TransformWorldToHClip
- TransformObjectToWorldDir / TransformObjectToWorldNormal
- GetWorldSpaceViewDir（未归一化）与 SafeNormalize

Lighting.hlsli 提供 GetMainLight 和 LightingBlinnPhong，显式接收生成的 SceneLightingData，没有额外 LightManager。Blinn–Phong 参数为 base_color/base_map、specular_color、shininess；计算漫反射和半程向量高光，背向光时无高光贡献。Lit 使用同一照明接口求值 PBR，Unlit 不要求 Lighting。

```hlsl
#include <renderlab/generated/parameters.hlsli>
#include "RenderLab/Lighting.hlsli"
output.positionCS = TransformObjectToHClip(input.positionOS,
    SceneObject.ObjectToWorld, SceneView.ViewProjection);
output.normalWS = TransformObjectToWorldNormal(input.normalOS, SceneObject.NormalToWorld);
DirectionalLight light = GetMainLight(SceneLighting);
```

CompileOptions.includeDirectories 挂载引擎根，shaderc 对应重复 --include DIRECTORY。use 先查当前模块目录，再查这些根，依赖内容参与缓存。

.material v2 直接保存 Shader UUID，内置与项目 Shader 使用同一引用规则；若 .meta 有 dependencies.shader 则必须相符。无覆盖的导入材质默认采用内置 Lit 的资源 ID。项目 Shader 需提供 Forward role、当前顶点语义和所需 Schema/资源语义；Renderer 按包/Pass 验证生成契约并缓存 BindingHandle，不检查显示名称、资源 symbol 或整个 Frame 字段列表。详见 [资产身份](shader-assets.md) 和 [参数接口](shader-parameters.md)。

材质值文件按修改时间重新读取；模型与项目 Shader 在创建 SceneRenderer 时加载。当前没有文件监视、模型实时重导入、编辑器或完整自动热重载集成。

## 验证入口

content.contracts 覆盖矩阵/法线/投影、UUID、缓存与依赖失效、移动/glTF/GLB、场景层级/序列化/选光/剔除、项目 Shader 使用引擎模块，以及 WIC 临时 COM 清理和调用者 STA 保持。

gpu.{dx12,vulkan}.scene 检查实际项目加载、纹理/高光读回，以及主光选择/强度/旋转、相机、对象启用/新增、材质替换、镜像变换和场景保存重开的行为。原验收保留，实际配置与结果见 [阶段计划](../design/phaseplan.md)。
