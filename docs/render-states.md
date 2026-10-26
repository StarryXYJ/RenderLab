# 固定功能状态与深度模板附件

RHI 的 `PipelineDesc` 使用 `RasterState`、`DepthState`、`BlendState`、`StencilState`，定义位于 `src/rhi/render_state.hpp`。完整不可变状态及可选 `depthStencilFormat` 都进入 PSO 缓存身份；Stencil reference 是动态命令，不进入 PSO。Shader 作者语法与材质配方仍按 [阶段计划](../design/phaseplan.md) 逐步接入。

## 深度、混合与光栅

附件格式独立于测试和写入。需要深度测试或写入时必须提供 D32/D32S8；关闭两者也允许保留已绑定的附件。`testEnabled=false, writeEnabled=true` 在两后端映射为启用原生深度、比较 Always；测试开启时使用声明的 CompareOp。默认 CompareOp 为 Reversed-Z 的 GreaterEqual，附件深度清除为 0。

Blend 独立控制 RGB/Alpha 的因子和操作，光栅状态提供 None/Front/Back 剔除、正面绕序及 RGBA 四位写掩码。Alpha 因子限制为 Zero/One/SrcAlpha/OneMinusSrcAlpha/DstAlpha/OneMinusDstAlpha，非法枚举和不支持因子在创建 PSO 前拒绝。BlendState 默认关闭，开启时默认 RGB SrcAlpha/OneMinusSrcAlpha、Alpha One/Zero；标准材质配方会另行明确 Alpha 合成策略。

## 模板

`Format::D32S8` 对应 DX12 D32_FLOAT_S8X24_UINT 与 Vulkan D32_SFLOAT_S8_UINT，预算按 8 字节/texel 估算；创建纹理时检查附件支持。基础设备报告列出该格式，但不强制所有设备支持。D32S8 不支持当前颜色复制 ABI、采样或 Storage。

模板状态提供 read/write mask 及 front/back 各自的比较、Fail/DepthFail/Pass 操作。`StencilState::Writes()` 保守判断写权限需求，不能把局部写掩码视为整附件覆盖。

`CommandList::StencilReference(uint32_t)` / `RasterContext::StencilReference(uint32_t)` 要求处于 Raster 录制且值在 0–255。每次绑定图形管线时原生 reference 重置为 0；Forward 每个 DrawPacket 还会显式设置自己的 `stencilReference`，避免沿用前一次绘制值。

## Graph

普通 `DepthTarget(handle, load, access)` 将相同初始化/权限应用到该附件的全部面。需要不同语义时使用：

```cpp
auto depth = pass.DepthStencilTarget(
    previousDepth,
    {rhi::LoadOp::Clear, rendergraph::AttachmentAccess::ReadWrite},
    {rhi::LoadOp::Load, rendergraph::AttachmentAccess::ReadOnly});
```

这会清除深度并保留模板。模板清除值由可选第四参数提供，默认 0。只读面必须 Load，Discard 暂不支持。两个面均 Clear 才能初始化新资源；任一面 Load 都要求先前整个组合附件已初始化，并保留其 producer。资源版本、依赖与屏障先保守跟踪整个附件，不能声明独立模板子资源。

绑定 PSO 时检查各面写权限；深度可写不意味着模板可写。原生附件始终使用保守的可写深度模板布局/状态，逻辑只读由状态与录制校验约束，不提供采样只读视图。Transparent Feature 根据 Draw 的实际深度/模板状态汇总权限，允许显式写入，不强制透明只能测试深度。

GPU 测试入口为 CTest `gpu.dx12.render_state` 与 `gpu.vulkan.render_state`；读回浮点像素验证独立深度写入、跨 Pass 模板保留/清除、reference 重置、双面操作、混合与颜色掩码。`rendergraph.contracts` 覆盖未初始化模板面、错误格式、producer 保留及只读面禁止写入。
