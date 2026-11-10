# 矩形面光源基础契约

当前只实现 CPU 场景文档与快照，不参与任何渲染或发光几何生成。实现状态和验证记录见 [阶段计划](../design/phaseplan.md)。

## 场景数据

Scene 对象可包含 `rectAreaLight`，字段 `radiance` 是非负、有限的线性 RGB 发射辐亮度，`width` 与 `height` 是局部空间正数全尺寸，`enabled` 默认为 true；省略字段依次默认 `[1,1,1]`、`1`、`1`。光面在局部 XY 平面，单面朝向局部 -Z，位置和变换来自所属对象；对象或祖先禁用时不提取。数据随 Scene 版本 1 保存和加载，旧场景不受影响。

Extractor 将全部启用面光源按稳定对象 UUID 排序，输出世界空间中心、完整 U/V 边向量、面积、朝向法线和辐亮度。层级非均匀缩放与旋转可能产生剪切，结果是平行四边形；面积为 `length(cross(U, V))`，正面法线为 `-normalize(cross(U, V))`。镜像时按变换后顶点绕序确定正面，不根据未缩放的旋转猜测。非正或非有限尺寸、负值或非有限辐亮度，以及退化或数值溢出的世界面积会拒绝。

这只是数据基础：当前 Path Tracing、Deferred 和 Forward 均不读取它。添加组件不会照亮场景、生成可见灯面或改变阴影；既有自发光材质仍按原路径命中计算。未来 NEE/MIS 必须明确灯组件与可见发光几何的身份、面积及单/双面语义，避免重合却无法匹配 PDF 或重复贡献；不能把两者自动视为同一个光源。还需要将灯变化纳入累积失效，并验证可见性、分量分类、采样概率和多灯情形。

## 独立采样数学

实验性 HLSL 位于 `shaders/m5/rect_area_sampling.hlsli`，没有加入运行时路径。`SampleRectEmitter` 均匀采样世界平行四边形；`RectEmitterPdfOmega` 将 `selectionPdf / area` 转换成 `selectionPdf * distance² / (area * cosLight)`，包含选中该灯的概率，采样点必须位于指定灯面。背面、重合点、退化面积和非有限结果返回零 PDF；小于等于 `1e-12` 的平方距离视为重合。调用方负责有效的 [0,1) 随机数、单位正面法线及与 BSDF 一致的测度。

`RectPowerWeight` 是每个技术各取一个样本的 power heuristic，在相同立体角测度下计算权重；不提供路径贡献、遮挡、发光体身份匹配或完整 MIS 策略。独立测试 `gpu.{dx12,vulkan}.rect_area_light` 将 GPU 均匀采样的单位漫反射结果与 CPU 双精度面积中点积分比较，并检查选灯概率、背面、重合、剪切、零 PDF、斜射与大 PDF 的 MIS 权重。仅测试固定几何和未遮挡单次采样，不代表实际光传输、时域或 Cornell Box 已接入。
