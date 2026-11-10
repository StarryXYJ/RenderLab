# 矩形面光源契约

Path Tracing 支持最多两盏解析矩形面光源，采用面积采样、遮挡射线与 BSDF 采样的 power-heuristic MIS；Forward、Deferred 和混合 Ray Tracing 暂不消费该组件。实现状态和验证记录见 [阶段计划](../design/phaseplan.md)。

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

存在显式上限：两盏以内、单面恒定辐亮度、均匀选灯；超限明确拒绝。不支持自动从发光三角形或纹理构造灯、重合灯、透明/Masked 参与光追遮挡，也不提供面光源的光栅实时光照。现有固定世界空间射线偏移在薄几何和极小/极大尺度有偏差；解析采样对极近距离使用阈值，极端近乎平行射线仍受浮点范围限制。没有 ReSTIR、光源分布预计算或光照烘焙。

## 光栅 LTC 数据准备

光栅面光源实现参考 HDRP 的 LTC 职责划分，但不复制 Unity Companion License 下的 Unity 源码或查找表。拟合矩阵与幅度使用论文作者的 [selfshadow/ltc_code](https://github.com/selfshadow/ltc_code/tree/31e5e96b54f98f33098f8503003119ba2231a1c6) 固定提交 `31e5e96b54f98f33098f8503003119ba2231a1c6`，原始 `fit/results/ltc_1.dds` 和 `ltc_2.dds` 原样保存在 `shaders/RenderLab/LtcMatrix.dds`、`LtcAmplitude.dds`；完整版权与许可为同目录 `Ltc_LICENSE.txt`，随 Shader 一起复制及安装。

参考论文：Eric Heitz、Jonathan Dupuy、Stephen Hill、David Neubelt，[Real-Time Polygonal-Light Shading with Linearly Transformed Cosines](https://eheitzresearch.wordpress.com/415-2/)，ACM TOG 35(4), 2016。原表为 64×64 RGBA16F DDS，参数顺序及拟合与 HDRP 自带表不同；加载器只接受该固定布局并转为 RGBA32F。`ltc.data` 验证格式、半精度转换及损坏输入；独立测试 Shader 已使用这两张表，但尚未接入渲染管线。`gpu.{dx12,vulkan}.ltc_eval` 将 LTC 与双精度面积积分对照，覆盖正视漫反射、金属、斜视剪切、跨地平线以及背面；四个有限场景的最大逐通道相对误差约 2.8%。漫反射 Fresnel 使用灯中心方向近似；该结果不代表所有粗糙度、视角或近距离条件均满足相同误差，也没有遮挡。
