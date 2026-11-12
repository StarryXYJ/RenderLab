# 矩形面光源契约

Path Tracing、Forward、Deferred 和混合 Ray Tracing 均支持最多两盏解析矩形面光源；Path Tracing 采用面积采样、遮挡射线与 BSDF 采样的 power-heuristic MIS。实现状态和验证记录见 [阶段计划](../design/phaseplan.md)。

## 场景数据

Scene 对象可包含 `rectAreaLight`，字段 `radiance` 是非负、有限的线性 RGB 发射辐亮度，`width` 与 `height` 是局部空间正数全尺寸，`enabled` 默认为 true；省略字段依次默认 `[1,1,1]`、`1`、`1`。光面在局部 XY 平面，单面朝向局部 -Z，位置和变换来自所属对象；对象或祖先禁用时不提取。数据随 Scene 版本 1 保存和加载，旧场景不受影响。

例如，下面的组件产生宽 1.2、高 0.8 的灯；完整 Scene 对象仍需提供 `id`、`transform` 等字段：

```json
"rectAreaLight": {
  "radiance": [12, 12, 12],
  "width": 1.2,
  "height": 0.8,
  "enabled": true
}
```

Extractor 将全部启用面光源按稳定对象 UUID 排序，输出世界空间中心、完整 U/V 边向量、面积、朝向法线和辐亮度。层级非均匀缩放与旋转可能产生剪切，结果是平行四边形；面积为 `length(cross(U, V))`，正面法线为 `-normalize(cross(U, V))`。镜像时按变换后顶点绕序确定正面，不根据未缩放的旋转猜测。非正或非有限尺寸、负值或非有限辐亮度，以及退化或数值溢出的世界面积会拒绝。

Path Tracing 直接对解析灯面做相交：正面发光，背面不发光且不透射；灯面在该管线可见并可遮挡阴影射线，无需也不应叠加相同的自发光 Mesh。Mesh 自发光仍由原有路径命中计算，并不自动等同或关联解析灯。解析灯之间及其与几何不应共面重叠；该情形的相交优先级与多个重合发光体的 PDF 未定义。面积灯变化会使路径累积和时域历史失效。

## 独立采样数学

HLSL 位于 `shaders/RenderLab/RectAreaLight.hlsli`，由 Path Tracing 与独立测试共同使用。`SampleRectEmitter` 均匀采样世界平行四边形；`RectEmitterPdfOmega` 将 `selectionPdf / area` 转换成 `selectionPdf * distance² / (area * cosLight)`，包含选中该灯的概率，采样点必须位于指定灯面。背面、重合点、退化面积和非有限结果返回零 PDF；小于等于 `1e-12` 的平方距离视为重合。调用方负责有效的 [0,1) 随机数、单位正面法线及与 BSDF 一致的测度。

`RectPowerWeight` 是每个技术各取一个样本的 power heuristic，在相同立体角测度下计算权重；不提供路径贡献、遮挡、发光体身份匹配或完整 MIS 策略。独立测试 `gpu.{dx12,vulkan}.rect_area_light` 将 GPU 均匀采样的单位漫反射结果与 CPU 双精度面积中点积分比较，并检查选灯概率、背面、重合、剪切、零 PDF、斜射与大 PDF 的 MIS 权重。独立数学测试只覆盖固定几何和未遮挡采样；实际路径、遮挡及多灯另由 `gpu.{dx12,vulkan}.area_light_render` 验证。

## 路径追踪常量准备

`renderer::PackPathRectLights` 将稳定顺序的快照打包到路径追踪现有 256 字节常量块的预留区：一个 float4 头（x 是灯数），其后每灯三个 float4，依次为 `center/radiance.r`、`edgeU/radiance.g`、`edgeV/radiance.b`。无灯时全零；最多两盏，超出明确拒绝，不静默丢灯。面积和单面法线由 U/V 的叉积恢复。Path Tracing 将数据写入常量块并参与累积与时域历史失效；限制是首版固定常量块容量，后续多灯应改用有界缓冲和明确的选灯概率。

`IntersectRectEmitter` 对有限平行四边形做解析射线相交，支持剪切和两面几何命中；调用方按正面判断是否发光。Path Tracing 对主射线、次级路径和阴影射线均使用该相交；GPU 专项覆盖正反面命中、边界外、平行、射线最大距离及剪切。

## 路径积分

每次可继续追踪的表面散射均匀选择一盏已启用矩形灯，均匀采样灯面并发送有限长度的遮挡射线。直接连接与 BSDF 命中解析灯时都使用包含选灯概率的立体角 PDF，并以 power heuristic 加权；相机直接看到的灯不加 MIS 权重。发射面与投射阴影的场景几何、其他解析灯均参与遮挡。NEE 仅在还剩下一段路径时进行，因此与 BSDF 命中遵循相同的四段路径上限。主命中点直接连接计入 primary direct，之后连接按已有首散射漫反射比例分到间接分量；未滤波总量独立累积。

存在显式上限：两盏以内、单面恒定辐亮度、均匀选灯；超限明确拒绝。不支持自动从发光三角形或纹理构造灯、重合灯、透明/Masked 参与光追遮挡，普通光栅阴影当前使用有限视场的中心投影近似。现有固定世界空间射线偏移在薄几何和极小/极大尺度有偏差；解析采样对极近距离使用阈值，极端近乎平行射线仍受浮点范围限制。没有 ReSTIR、光源分布预计算或光照烘焙。

## 光栅 LTC 数据准备

光栅面光源实现参考 HDRP 的 LTC 职责划分，但不复制 Unity Companion License 下的 Unity 源码或查找表。拟合矩阵与幅度使用论文作者的 [selfshadow/ltc_code](https://github.com/selfshadow/ltc_code/tree/31e5e96b54f98f33098f8503003119ba2231a1c6) 固定提交 `31e5e96b54f98f33098f8503003119ba2231a1c6`，原始 `fit/results/ltc_1.dds` 和 `ltc_2.dds` 原样保存在 `shaders/RenderLab/LtcMatrix.dds`、`LtcAmplitude.dds`；完整版权与许可为同目录 `Ltc_LICENSE.txt`，随 Shader 一起复制及安装。

参考论文：Eric Heitz、Jonathan Dupuy、Stephen Hill、David Neubelt，[Real-Time Polygonal-Light Shading with Linearly Transformed Cosines](https://eheitzresearch.wordpress.com/415-2/)，ACM TOG 35(4), 2016。原表为 64×64 RGBA16F DDS，参数顺序及拟合与 HDRP 自带表不同；加载器只接受该固定布局并转为 RGBA32F。`ltc.data` 验证格式、半精度转换及损坏输入；两张表已接入光栅直接照明；独立测试 Shader 用于数值核对。`gpu.{dx12,vulkan}.ltc_eval` 将 LTC 与双精度面积积分对照，覆盖正视漫反射、金属、斜视剪切、跨地平线以及背面；四个有限场景的最大逐通道相对误差约 2.8%。漫反射 Fresnel 使用灯中心方向近似；该结果不代表所有粗糙度、视角或近距离条件均满足相同误差，也没有遮挡。


## 光栅直接照明

Forward、Deferred 和 Indexed Forward 使用共享的场景灯快照与 LTC 查表计算无遮挡的矩形灯直接照明；每帧最多两盏，超限时明确拒绝。灯面朝向和剪切平行四边形与路径追踪一致。灯亮度为线性辐亮度；不需要预烘焙，LTC 拟合表是材质模型的预计算数据，与场景无关。内置 Lit 支持此照明，自定义 Shader 需显式实现相应契约。

光栅管线从同一灯数据绘制独立灯面：正面显示辐亮度，背面为不透光黑面，参与相机深度测试；路径追踪继续使用解析相交，不绘制这份光栅几何。Forward、普通 Deferred 及透明物体使用中心投影阴影近似，所有光栅路径都不产生面灯间接光，混合光追反射射线能与解析灯面比较最近命中并显示正面辐亮度，背面遮挡；灯变更会重置反射历史。反射射线命中的普通物体对每盏灯进行一次均匀面积采样并追踪有限遮挡射线，场景几何和其他解析灯可遮挡；该单样本估计可能产生噪声和高亮离群值，使用现有反射累积与显示重建。普通光栅路径在视场外、近平面附近和光源有较大面积时可能漏光，不能把此阶段当作 HDRP 完整面光源实现。`gpu.{dx12,vulkan}.raster_area_light` 验证 Cornell Box 的开关、双灯、灯面正反面与相机遮挡和不同光栅提交路径一致性，使用显示输出而非无偏 HDR 参考；数值拟合只覆盖上述有限测试场景。

`gpu.{dx12,vulkan}.area_light_reflection` 在 Cornell Box 对照灯的亮度、方向、几何遮挡及小灯照亮反射命中物的贡献，并检查灯变化使混合反射的累积与时域历史失效；这是有限场景验证，未覆盖全部表面材质与极端尺度。


## 光栅面灯阴影

每盏灯从中心沿灯面正面方向绘制一张 1024² reversed-Z 阴影图，投影物独立于方向光，从所有启用实例按材质 ShadowCaster 规则筛选；Masked 使用其阴影 pass 的 alpha 裁剪。Forward、Deferred、Indexed Forward 和透明 Forward 查询各灯的阴影图。采用自实现的 PCSS 式近似：在接收点处的灯面投影附近，以中心及 4×4 样本估计遮挡物距离，再用 4×4 样本过滤，半影随灯尺寸和遮挡距离变化。遮挡搜索和最终过滤均对深度比较结果做双线性加权，避免随纹素边界出现阶梯。接收面深度梯度由表面法线和灯投影计算，以减少斜面自阴影并保持 Forward/Deferred 一致。它使用单一中心投影、有限样本和最大 0.12 UV 滤波半径，并以灯两条边长近似滤波轴；搜索范围收窄可减少远处无关墙面引起的暗斑，但靠近灯源的细小遮挡物更容易漏检；剪切、近距离和大面积灯的半影并非精确积分。每灯 170° 视场、0.01 世界单位近平面以及固定偏移会造成边界漏光、自阴影或失真。没有 EVSM、阴影缓存和可配置质量；透明接收和 Masked 的全裁剪/投射开关由 `gpu.{dx12,vulkan}.surface` 专项覆盖；性能及跨设备验收尚未完成。`gpu.{dx12,vulkan}.area_shadow_filter` 验证半影随灯尺寸与遮挡距离变化以及全亮/全暗端点。2026-11-10 的 Cornell 部分遮挡基线中，DX12 显示通道和为光栅 6358、旧版 16 射线 Ray Query 7668，只说明该有限场景的量级，非 HDR 或逐像素误差验收。`gpu.{dx12,vulkan}.raster_area_light` 验证无方向光的遮挡、双灯独立及不同光栅提交路径一致性；混合光追测试检查与 Ray Query 的全遮挡结果。


## 混合光追面灯遮挡

Ray Tracing 管线对不透明主表面使用 Ray Query，分别在每盏灯上使用固定的 8×4 抖动分层点，追踪到有限灯面距离并计算可见比例；其他解析灯与不透明投影几何也可遮挡。这一比例分别乘以各灯的 LTC 直接光。它能产生近似软阴影，但没有按 BRDF/立体角加权，采样仍可能出现分层和走样，尤其在近灯、大遮挡或高光场景；不透明接收面使用深度与法线引导的 3×3 空间滤波减少阶梯，但细小阴影仍可能被软化。采样固定以避免静止场景逐帧闪烁；它不是无偏的面积积分，也没有阴影时域滤波。每像素最多增加 64 条阴影射线，尚未完成跨设备性能验收。透明表面使用上述光栅阴影图而非 Ray Query；阴影投射遵循当前不透明几何及投影策略，不支持透明或 Masked 光追遮挡。`gpu.{dx12,vulkan}.area_light_reflection` 对照完全、部分遮挡与双灯独立性、可见性等级及静止帧稳定性。可选 `SceneRenderOptions::shadowDiagnostics` 在 Ray Tracing 管线提供未空间滤波的 RGBA8 回读，R 为方向光、G/B 为两盏面灯，行距使用 `TextureRowPitch`；需等待提交完成。


`SceneRenderOptions::sceneHdrDiagnostics` 可读取显示变换前的线性 RGBA32F（`SceneReadback::sceneHdr`），用于跨管线对照。Cornell Box 的 DX12 40×40 有限测试以 Path Tracing 4096 spp 的 primary direct 为参考，排除参考总亮度不低于 5 的像素后，平均亮度为路径 0.1441、Deferred 0.1248、混合 Ray Query 0.1294；逐通道 MSE 分别约 0.125 和 0.124。路径主射线带抖动、光栅边界不抗锯齿，因此这些 MSE 包含几何边缘差异，不能单独解释为阴影估计误差，也不能推及其他场景。对 926 个按参考图筛掉高亮、几何边缘及局部梯度的像素，逐通道 MSE 为光栅 0.000566、混合 0.0000526；此筛选不覆盖复杂边界。双灯变体以 1024 spp 路径 primary direct 为参考，平均亮度路径 0.03464、光栅 0.02852、混合 0.02946。2026-11-12 阴影平滑修复后，同一 DX12 单灯场景的光栅、混合直接光均值为 0.1281、0.1295；上述 926 个内部像素的逐通道 MSE 为 0.0000553、0.0000488。`gpu.{dx12,vulkan}.area_light_render` 执行此对照与能量范围检查。


## 有限性能测量

下表为 2026-11-10、旧版 16 点面灯阴影的历史基线，不代表当前修复后的耗时。使用 RTX 5060、驱动 32.0.15.9174、Release、验证层关闭、512×512、固定相机，预热 10 帧后测量 30 帧。零灯为 Cornell 顶灯禁用；单灯为原场景；双灯将其拆为两个较小灯，几何相同。以下是对应 GPU pass 平均耗时（毫秒），不等于端到端帧时间或通用性能结论：

| 后端 | 管线与 pass | 零灯 | 一灯 | 两灯 |
| --- | --- | ---: | ---: | ---: |
| DX12 | Forward.Opaque | 0.042 | 0.166 | 0.288 |
| Vulkan | Forward.Opaque | 0.041 | 0.161 | 0.290 |
| DX12 | RayTracing.Shadows | 0.018 | 0.399 | 0.816 |
| Vulkan | RayTracing.Shadows | 0.013 | 0.407 | 0.809 |

每张光栅面灯阴影图 pass 约 0.03–0.04 ms。光栅 pass 同时包含 LTC 和阴影滤波；Ray Query pass 包含方向光检查与面灯射线，双灯尺寸也不同，不能直接将差值解读为单独算法的精确成本。混合管线当前即使没有透明表面也会生成光栅灯阴影图。性能数字仅测一张显卡、一个场景和分辨率；Intel 核显另外以 DX12、验证层开启、256² 运行 Forward 和混合管线各 8 帧，均无验证错误，但这不是画质或性能验收。Intel 核显 Vulkan 在创建 Runtime 时因现有 64 位有效时间戳要求被拒绝，未进入面灯渲染；动态遮挡与长期运行仍未验收。结果和零灯/双灯临时场景位于 `out/area-light-benchmark`，生成命令见 [构建手册](../manual/build.md#面光源短测)。


2026-11-12 在同一 RTX 5060、DX12、512²、单灯 Cornell 场景、验证层关闭和 10 帧预热后测量 30 帧：修复后 `Forward.Opaque` 约 0.235 ms（旧版约 0.166 ms）；混合管线 `RayTracing.Shadows` 约 0.834 ms（旧版约 0.399 ms），`Deferred.Lighting` 约 0.147 ms（旧版约 0.106 ms）。这是有限短测，32 点及空间滤波提高了成本，不能推及其他 GPU、分辨率或灯数；数据位于 `out/area-shadow-smooth`。
