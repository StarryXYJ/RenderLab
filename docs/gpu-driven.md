# GPU Driven 与间接提交

几何提交方式与 Forward/Deferred/光追组合独立。CPU 直接绘制是正确性与性能基线；GPU 动态数量留在 GPU 上消费。各能力的实施与验收状态只在 [阶段计划](../design/phaseplan.md) 维护。

## 公共间接命令

`rhi::CommandList::DrawFrom` 与 `rendergraph::RasterContext::DrawIndirect` 接收参数 Buffer、字节偏移、最大命令数、是否索引绘制，以及可选 Count Buffer/字节偏移。两类参数都是紧密排列的公共 ABI，不接受任意原生命令签名或可变 stride。

| 字段顺序 | 大小 | 原生映射 |
| --- | --- | --- |
| `DrawArguments { vertexCount, instanceCount, firstVertex, firstInstance }` | 16 字节 | D3D12 DRAW / VkDrawIndirectCommand |
| `DrawIndexedArguments { indexCount, instanceCount, firstIndex, int32 vertexOffset, firstInstance }` | 20 字节 | D3D12 DRAW_INDEXED / VkDrawIndexedIndirectCommand |

无 Count 时执行 `maxCount` 条命令；有 Count 时执行 `min(GPU uint32 count, maxCount)` 条。零 Count 或某条命令的零 instanceCount 不产生片元；最大命令数本身必须非零。参数 Buffer 必须容纳最大命令数的完整范围，不能依据尚未读取的 GPU Count 缩减容量。参数与 Count 偏移均为 4 字节对齐，Count 范围为 4 字节。

DX12 使用两个无 Root Signature 的固定 CommandSignature，分别调用 ExecuteIndirect；Vulkan 使用 Draw[Indexed]Indirect[Count]。`Runtime::GetIndirectSupport()` 返回已启用的多绘制上限、Count 和非零 firstInstance 能力。Vulkan 按支持显式启用 multiDrawIndirect、drawIndirectFirstInstance 和 Vulkan 1.2 drawIndirectCount；不支持时录制拒绝超限/Count 请求。GPU 参数内容无法在录制时检查，生产者必须保证实例、顶点和索引范围合法，并且仅在 firstInstance 能力启用时写非零值。

对象索引通过 `DrawParameters.hlsli` 的 `GetInstanceIndex` 消费 firstInstance 和实例内偏移。DXIL 的 SV_InstanceID 不含 firstInstance，需要 SM 6.8 的 SV_StartInstanceLocation 相加；DXC 默认 SPIR-V 的 SV_InstanceID 已含 firstInstance。该 helper 隔离这一差异，`shaderInstanceIndex` 能力明确检查 DX12 SM 6.8；基础非索引样例仍用 SM 6.0。不依赖原生 DrawID：公共命令未承诺 DX12/Vulkan 的 DrawID 映射，也不把命令序号当作对象身份。多条命令可以重复或打乱实例范围。直接 Draw/DrawIndexed 也接受 instanceCount、firstInstance，后者另接受有符号 vertexOffset，以便使用相同 Shader 和数据建立参考。

## Graph 与生命周期

参数和 Count 都必须声明 `Use::Indirect`，且声明范围覆盖实际命令/Count 范围。Compute 写入声明 Storage，清零/上传声明 CopyDest；Graph 产生写后间接读取屏障。Execute 只解析已声明的类型化 handle，不读取本帧 Count 决定 CPU 命令数量。

命令包拥有参数与 Count 的强引用。取消录制释放引用；提交后由 Runtime 保留至完成点退休，不能因 CPU 丢弃句柄而提前回收。布局与绑定仍为不可变快照。

## 验证入口

`gpu.dx12.indirect` / `gpu.vulkan.indirect` 使用同一真实 GPU Shader，比较直接、CPU 参数与 Compute 参数的完整 RGBA8 图像。覆盖索引/非索引、多条固定命令、Count 0/1/2/3/99 截断、非零 firstVertex/firstIndex/vertexOffset/firstInstance、跨命令实例身份和单命令多实例；同时检查偏移/容量/能力拒绝、未声明 Count、取消与提交退休后的资源引用。测试要求实际设备支持这组可选能力，不静默降级。
