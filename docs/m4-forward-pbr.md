# M4.3：Forward PBR 与 HDR 显示

Lit、Unlit 和显示变换都是普通 `.shader` 资产，经过同一 Shader/Material 准备链路。Renderer 负责场景资源、全局数据和 Graph 访问声明；材质模型没有变成 C++ Renderer 分支。原 Blinn–Phong 和项目 Copper Shader 仍可使用，输出同样进入 HDR 显示链路。

## 表面与内置 HLSL

| 文件 | 契约 |
| --- | --- |
| Math.hlsli、BRDF.hlsli | 不依赖生成参数的数值函数；Schlick Fresnel、GGX NDF、相关 Smith visibility、Lambert 漫反射 |
| Surface.hlsli | MaterialSurface 值：线性 baseColor、alpha、世界法线、metallic、perceptualRoughness、occlusion、emission |
| MaterialSampling.hlsli | 光栅 RasterSamplingContext 与 EvaluateMaterial；纹理 SampleGrad，求值结果不含照明 |
| Lighting.hlsli、ImageBasedLighting.hlsli | 主方向光与预滤波环境的照明求值 |
| Color.hlsli、ToneMapping.shader | 曝光与 ACES fitted 曲线，输出线性值给 sRGB 附件 |

Lit 的稳定数值属性为 `base_color`、`metallic`、`roughness`、`normal_scale`、`occlusion_strength`、`emission`。资源 ID 为 `base_map`、`metallic_roughness_map`、`normal_map`、`occlusion_map`、`emission_map`、`base_sampler`。颜色纹理声明 srgb；MR、Normal、Occlusion 使用 RGBA8 线性格式；同一源图像按实际使用的颜色空间分别创建 GPU 投影。白色与精确 `(0.5,0.5,1)` 平法线是内置默认资源。导入桥按类型化 semantic 映射到属性，不依赖这些 ID 的拼写。

MR 使用 glTF 通道约定：G 为感知粗糙度，B 为金属度，分别乘对应标量。粗糙度求值钳制到 `[0.045,1]`，金属 F0 为基础色，非金属 F0 为 0.04；AO 只影响间接光，Emission 独立相加。Unlit 直接输出基础色乘纹理，可以保存 HDR 基础色，仍接受曝光和显示变换。

光栅入口在分支前计算 UV/世界位置导数。Normal Map 从 UV 导数重建切线框架，支持镜像 UV、非均匀缩放与背面翻转；退化 UV 回退几何法线。此路径不使用 glTF 的 authored TANGENT，也不保证与 MikkTSpace 烘焙完全相同。未来 Ray Hit 需显式 LOD/纹理 footprint，不能直接调用光栅导数入口；MaterialSurface 与 BRDF 数值函数可以复用。

## 材质与导入

无用户覆盖的 glTF 材质现在采用 RenderLab/Lit，导入 metallic/roughness 因子与纹理、normal scale、occlusion strength、emissive 因子/纹理。缓存版本和 importer key 已升级；旧 Library 可重建，用户 `.material` 不被覆盖。当前 Opaque、UV0、float 顶点、point-clamp 的约束不变；normal scale 支持 `[0,2]`，超范围明确报错，其他因子遵循 glTF 范围。

内置和项目材质均以 Shader UUID 保存引用，显示名与文件路径可自由调整。`.material` 为 v2，若 `.meta` 声明 `dependencies.shader` 则必须与正文一致。程序角色、Schema 和资源语义在准备前校验，见 [Shader 资产](shader-assets.md) 与 [参数接口](shader-parameters.md)。

`Preparation.deferredResources` 显式列出 Graph 尚未分配的外部资源 BindingHandle。Prepare 校验 handle 属于当前包/Pass、不是材质属性且没有同时绑定，然后只准备程序/PSO/常量；录制时由 Graph 绑定声明过的实际资源。该结果不应直接交给 CreateGroup，必须先补齐这些绑定。ToneMapping 的 `HdrColor` 由调用方显式解析局部接口后使用此入口，不创建占位纹理。

## 环境光

Scene 的可保存字段：

```json
"environment": { "image": "图像资产 UUID 或空字符串", "intensity": 3, "rotation": 0 },
"exposure": 0
```

环境图是 lat-long sRGB 图像，采用与图像导入相同的 WIC 解码；不支持 `.hdr`/`.exr` 输入。空 image 使用 Scene.ambient 作为均匀线性 radiance。intensity 为非负比例（上限 10000），rotation 为绕 Y 的度数，exposure 为 `[-16,16]` stops。强度/旋转随 Lighting 常量更新；换图或改变均匀环境颜色才重新预滤波和上传。

预滤波在 Renderer 准备阶段确定性执行，不在 Graph Execute 中运行。每个图块 64×32，纵向 6 层 GGX 粗糙度 `[0,1]` 加一层 cosine 卷积 irradiance/pi，128 个 Hammersley 样本；BRDF scale/bias LUT 为 64×64、128 样本，采用 GGX 与 Schlick-Smith 的 split-sum 近似。使用单 mip/layer 的 RGBA32F 2D 图集，Shader 显式双线性 Load、经度环绕、纬度钳制和粗糙度层插值。该低分辨率近似用于基础 IBL，无环境重要性采样、原生 cubemap/mip、镜面多次散射补偿或实时探针。

CPU 预计算表只保存固定尺寸的 sRGB 解码、输出方向/切线基及样本方向，不缓存输入图像或改变
每像素样本的累加顺序。空图直接填充 fallback；固定 BRDF LUT 首次调用生成，后续返回拥有独立
像素存储的值副本。函数局部静态初始化保证并发首次调用安全，缓存不持有 Device/GPU 对象，
因此可跨 Renderer/后端重建复用。BRDF 半向量按 roughness 行生成一次。`environment.contracts`
保留改造前独立标量算法，覆盖非均匀/单像素/奇数尺寸、接缝脉冲、恒定能量、确定性、并发初始化、
副本编辑和非法输入；数值与性能证据见 [基础优化测量](performance/b4-foundation.md#b44a-环境预计算)。

`RENDERLAB_ENVIRONMENT_SIMD` 是默认关闭的编译选项。开启时固定 ISPC 1.28.2，在图块级组织
uniform 工作，一个输出像素对应一个 lane，保留各像素 128 次样本的累加次序；未滤波 tile 0
仍用原 C++ 采样，卷积层使用 SSE2/AVX2 运行时分派。只接受 SSE2 单目标或 SSE2/AVX2 组合，
不把高 ISA 指令泄漏到普通 C++，不引入后台 task。核函数与 64×32、六层、128 样本契约同时固定，
C++ 静态断言防止单边修改尺寸。输入仍由公共函数校验，内部范围核只消费合法像素区间。
普通 C++ 内核同样采用可局部内联的分量运算，以及依据 atan2 范围证明的经度单步环绕，
不在每个样本调用图外通用 Vec3 运算或重复做整数模；操作的浮点结合顺序与原始参考保持一致。
因而 SIMD 收益必须与这条整理后的 C++/MSVC 优化路径比较，不能只对比改造前的间接调用成本。

系统数学函数和关闭 FMA/fast-math 不意味着逐位相同；高分辨率高频源会放大方向运算的舍入。
独立参考测试对常规非均匀图使用绝对分量误差 `2e-6`，对 8192×1 / 1×8192 边界使用 `2e-4`；
这些是已测 fixture 的接受界，不是任意输入的数学误差证明。未滤波层和 BRDF 保持精确参考，
暖调用确定性、常量能量、范围 guard/空区间/尾部单独验证。SIMD 只另存约 82.5 KiB 的显式
标量布局适配表，不将库类型暴露到公共 API 或用 reinterpret_cast 假设 Vec3 ABI。
普通 C++ 仍保留逐元素相同的参考路径；开关与固定编译器校验见 [构建](../manual/build.md#可选环境-simd)。

## HDR 与 Graph

Lighting 的 `EnvironmentParameters` 保存强度和弧度旋转。View/Object/Lighting/Shadow 分块由 `.params` 生成 C++/HLSL 契约，环境图集和 LUT 通过固定资源语义解析为 Lit Forward Pass 的绑定 handle；HLSL 库接受显式值和纹理参数。

SceneRenderer 创建 RGBA32F CameraColor 和 D32 CameraDepth，普通 Forward 绘制线性 HDR；组合结束后添加 `Display.ToneMapping`，读取 CameraColor，执行 `exp2(exposure)` 与 ACES fitted 曲线，将结果写入导入的 DisplayColor。显示目标必须为 RGBA8/BGRA8 sRGB，只由附件进行一次编码。读回和 Present 都消费 DisplayColor。Forward 的颜色快照若开启，保存的是 Tone Mapping 之前的 HDR；原 Custom/RHI Demo 的路径不变。

RHI 新增 Rgba32Float，`FormatBytes` 与 `TextureRowPitch(width, format)` 统一上传/读回 texel 大小；省略 format 保持 RGBA8 默认。DX12 与 Vulkan 都查询格式用途支持，创建失败明确诊断。RGBA32F 用于正确性基线，开销为每 texel 16 字节；后续可按测量增加 RGBA16F。仍没有 mip/layer、Alias 或独立 HDR 显示器输出。

## 验证与复现

运行命令见 [构建手册](../manual/build.md#m43-pbr-参考场景)，实际配置与通过情况只记录在 [阶段计划](../design/phaseplan.md)。

- pbr.contracts：均匀 HDR/白色环境卷积保持能量、BRDF LUT 有限有界、不同格式行间距、内置 Shader 双目标编译。
- content.contracts：PBR 参数/图像引用缓存往返、内容失效和非法因子诊断，并保留已有导入与场景合约。
- gpu.{dx12,vulkan}.pbr：固定材质球、金属度/粗糙度/法线/MR 图变化、IBL 单独照明及旋转/关闭、Emission、曝光、Unlit 与光源独立、场景往返、RGBA32F 非整齐行间距逐位读回，以及仅保留材质 generation 时跨退休帧复用 PSO、释放 generation 后回收 PSO。
- gpu.{dx12,vulkan}.pbr_window：完整 Pbr.rlproject 的窗口启动、首帧独立纹理解码前后保持宿主 STA、resize/minimize/restore 与退出；COM 所有权见 [场景资源契约](m4-scene.md#静态-gltf-与缓存)。自动窗口测试不代替手动键盘与标题栏拖动验收。
- Unlit HDR 颜色经过曝光/ACES/sRGB 的 GPU 输出与独立 CPU 参考比较，误差最多一个 8-bit code；其余图像比较针对行为变化，不冒充离线 BRDF ground truth。

Vulkan 当前可能提示 Unlit 顶点未消费 NORMAL 属性（公共静态网格布局仍提供它），不影响结果；验收禁止 validation error。阴影与 Masked/透明接入见 [M4.4](m4-shadows-alpha.md)，调试面板与 Player 见 [工具](m4-tools.md)；天空绘制按后续需求推进。
