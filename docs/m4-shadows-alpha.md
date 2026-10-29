# M4.4：方向光阴影与 Alpha 材质

场景仍使用普通 Shader/Material 和 Forward 组合。`ShadowFeature` 注册在 BeforeOpaque，生成 `MainLightShadow`；Opaque 与 Transparent 的 Lit 绘制显式读取它，之后统一执行 HDR Tone Mapping。Shader 负责裁剪与着色，Renderer 负责角色分类、排序和资源声明。

## 单张方向光阴影

DirectionalLight 支持保存 `shadows`（默认 false）、`shadowResolution`（128–2048 的 2 次幂，默认 512）、`shadowBias`（归一化深度偏移，0–0.1，默认 0.001）和 `shadowNormalBias`（世界单位法线偏移，0–1，默认 0.01）。沿用按 UUID 选择唯一主方向光的规则；没有选中光、强度为零或未启用阴影时，Shadow 接口的阴影开关为零。

Extractor 保留所有继承启用的静态实例作为 shadowCasters，包括相机剔除的实例。Renderer 根据材质角色和 Shadow Pass 是否存在过滤投影绘制。`FitDirectionalShadow` 使用这些世界 AABB 拟合正交投影，光源方向为深度增加方向，遵守 0..1 Reversed-Z。包围盒每轴留至少 0.1 世界单位及 5% 边距，XY 中心按 texel 对齐，竖直光与零体积包围盒均有有限结果。包围盒改变仍会改变比例；该实现不承诺级联或动态场景下的完全稳定性。

阴影 Pass 使用 RGBA32F 颜色附件记录 `SV_Position.z`，同时用 D32 做最近片元可见性。颜色 R 是可采样深度，空白清零表示无阻挡；D32 不被采样。这样保持当前单颜色附件、单 mip/layer 和不可采样深度的 RHI 契约。512² 两个附件的逻辑大小合计 5 MiB，实际分配另含后端对齐。

`Shadow.params` 生成 `MainLightShadowMatrix` 和 `MainLightShadowParameters`（texel 大小、深度偏移、法线偏移、启用）。Lit Forward Pass 通过 `[binding(MainLightShadow)]` 解析外部资源 handle，使用 deferredResources 在 Graph 录制时绑定真实纹理；无阴影时仍提供清零的 1×1 资源。Forward 与 Deferred 共用 3×3 双线性比较 PCF：先比较深度，再按 texel 中心间的子像素位置插值可见度，将重复 tap 合并为 4×4 共 16 次 Load，权重和为 9。相比原有整格点 9 次 Load，采样成本增加，但边缘不再仅在跨 texel 时跳变；不能先插值深度再比较。反转 UV 的 Y，与双后端的统一视口约定一致；每个 tap 用接收平面深度修正防止斜面自阴影，越界 tap 和投影外片元视为可见。阴影只乘主光直接 BRDF，IBL 与 emission 不受影响。

Editor 视口默认覆盖阴影分辨率为 1024²，移除早期原型的 128² 限制，不改写场景保存值；1024² 两个附件的逻辑大小合计 20 MiB。Sandbox/Player 仍按场景设置或显式质量档位选择。提高分辨率与连续 PCF 用于减轻单张阴影图的阶梯和跳变，不解决大范围场景的 texel 密度不足，也不能代替对具体几何的 bias 调整。

## 材质角色与导入

Lit/Unlit 使用 Surface、Blend 与独立 AlphaClip 属性，保留相同材质布局；只有标记 keyword 的 AlphaClip 是静态轴：

| 属性组合 | 主绘制 | 阴影 |
| --- | --- | --- |
| Opaque、AlphaClip=false | 忽略 base alpha，开启深度写入 | 使用 ShadowCaster role 投影 |
| Opaque、AlphaClip=true（Masked） | 计算 BaseColor.a × BaseMap.a；小于 alpha_cutoff 时 clip，其他片元视为不透明 | 使用相同 alpha 纹理与阈值 |
| Transparent、Blend=Alpha | straight alpha，在线性 HDR 混合；默认深度测试开启、写入关闭 | 标准配方不投影 |

`alpha_cutoff` 为 [0,1]，默认 0.5；恰好等于阈值的片元保留。光栅采样的导数在裁剪前计算。Unlit 可以投射阴影，但自身不接收光照或阴影。

glTF 支持 OPAQUE、MASK、BLEND 及 alphaCutoff，默认遵守 glTF 的 OPAQUE/0.5。导入桥通过类型化 semantic 设置相应属性；用户覆盖 .material 时以覆盖材质为准。Importer 与 CBOR 缓存版本为 v3，旧缓存可重建，alpha 模式/阈值参与内容校验。

SceneRenderer 消费类型化 render 策略分类，按 role 选择 Forward/ShadowCaster；Pass 名称自由。自定义 Shader 显式声明所需 Schema/资源语义，库通过参数接收值，无需复制整个 Frame。缺少 ShadowCaster 时不投影。SurfaceForward 提供默认混合/深度状态，Pass 可覆盖各字段，包括透明深度写入；Renderer 不根据 Blend 反推状态。完整规则见 [渲染状态](render-states.md) 与 [参数接口](shader-parameters.md)。

透明 distance 为世界 AABB 中心变换到 view 后的非负 -Z；先按队列、同队列稳定地从远到近排序。绘制使用已有 opaque 深度，Graph 按最终状态汇总附件读写权限。不能正确解决互相穿插或凹形透明模型，没有 OIT、折射、透射或彩色阴影。OpaqueColor/AfterTransparentColor 快照位置仍遵循 [Forward 组合](forward-pipeline.md)，均在 Tone Mapping 前。

## 验证入口与限制

- surface.contracts：竖直主光、投影边界、Reversed-Z 方向与退化包围盒。
- content.contracts：alpha 模式/阈值导入缓存、非法值、相机剔除后仍保留投影候选。
- gpu.{dx12,vulkan}.surface：合成半平面阴影边缘的 64 个子 texel 采样与解析线性过渡比较、斜面接收深度修正；阴影只降低直接光、光旋转、分辨率、视野外投影、Masked 不写入颜色/深度且完全裁剪时不投影、透明排序/零 alpha/遮挡，以及独立 CPU 线性混合→ACES→sRGB 数值参考（最多一个 8-bit code）。
- gpu.{dx12,vulkan}.surface_window：完整 Shadow.rlproject 的启动、STA 保持、resize/minimize/restore 与退出。

Vulkan 基础设备明确检查并启用 `shaderDemoteToHelperInvocation`，满足 DXC 对 HLSL clip 的 SPIR-V 要求。DX12 可能报告阴影清零值与原有附件优化 clear 值不匹配的性能提示；Vulkan 可能提示公共顶点布局中的 NORMAL 未被 Shadow/Unlit 消费。验收不允许 validation error，实际配置和结果统一记录于 [阶段计划](../design/phaseplan.md)。

本路径不增加级联、深度采样、深度专用 PSO、Sky、DepthPrepass、Bloom 或其他后处理；后续按用途与测量增加。运行方法见 [构建手册](../manual/build.md#m44-阴影与透明场景)。
