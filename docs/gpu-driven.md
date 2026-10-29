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

`BindingDesc::count` 默认 1，当前仅允许 sampled-texture 数组（最多 64 项）。`Binding::arrayElement` 与 Graph `BindTexture` 的最后一个参数指定数组元素；不可变组必须提供全部元素，未用位置由调用者填充有效默认纹理。布局按 register 区间检查重叠，不仅比较首 slot。DX12 分配连续描述符并以首元素作为 Root Table；Vulkan 使用固定 descriptorCount 和逐元素写入，不使用运行期变长数组、update-after-bind 或 partially-bound。

`Runtime::SampledTextureArrayLimit()` 区分硬件特性与当前实现范围；0 表示不可用。DX12 要求 ResourceBindingTier 2，Vulkan 显式启用 shaderSampledImageArrayNonUniformIndexing，并检查 sampled-image 限额至少为 80（64 个材质纹理加 16 个其余 sampled 绑定余量）。Shader 用 NonUniformResourceIndex 标记可能在同一波内变化的资源下标；调用者保证下标小于 count。计数、缺项、重复元素、错误类型、空资源和越界在原生描述符创建前拒绝。

数组组持有每个 View/Texture；每个版本独占描述符分配，旧命令未退休时不能原地更新。DX12 heap 页和 Vulkan pool/set 块只在没有外部使用时复用，不负责纹理驻留；所有权仍由不可变组与命令包承担。描述符占用与容量按元素数统计。Graph 必须声明每个可能动态访问的纹理，执行时逐元素验证。

参数和 Count 都必须声明 `Use::Indirect`，且声明范围覆盖实际命令/Count 范围。Compute 写入声明 Storage，清零/上传声明 CopyDest；Graph 产生写后间接读取屏障。Execute 只解析已声明的类型化 handle，不读取本帧 Count 决定 CPU 命令数量。

命令包拥有参数与 Count 的强引用。取消录制释放引用；提交后由 Runtime 保留至完成点退休，不能因 CPU 丢弃句柄而提前回收。布局与绑定仍为不可变快照。

## 场景索引基线

`SceneRenderOptions::submission` 独立于 Forward/Deferred/RayTracing 管线。Direct 保留原有材质与每对象提交；IndexedDirect 以公共表和同一绑定快照逐条直接绘制，CpuIndirect 以 CPU 已知可见列表生成索引参数后执行多条间接绘制。CLI 分别为 `--submission direct|indexed-direct|cpu-indirect`，默认 Direct。

GpuCulled（CLI `--submission gpu-culled`）在 Compute 中生成可见列表、索引参数与 Count，要求已启用 Count Buffer。正常帧不把本帧数量同步读回 CPU。

IndexedScene 从提取快照建立对象、去重材质、纹理和可见实例表。对象记录为 256 字节（当前/法线/历史矩阵、世界 Bounds、网格范围和材质索引），材质为 128 字节；矩阵明确采用 row_major 存储和列向量乘法。纹理下标只在当前不可变快照内有效，不是资产身份或永久物理描述符。材质可合并相同值与纹理版本；CPU Material/Scene 继续拥有编辑权威。

静态网格按导入身份打包为一个顶点和索引 Buffer，替换时创建新版本，命令保活旧版本。首次启用或几何集合变化会同步上传一次；相机、对象变换和普通连续帧不重传几何。索引路径与原始每网格 Buffer 当前同时保留，增加内存以便独立选择基线；光追表仍保持 M5 的独立格式/纹理 footprint 边界。

Frame/View、Lighting、Shadow 沿用生成参数 ABI，内置材质通过公共采样/BRDF/IBL/阴影库求值。GPU 表支持内置 Lit/Unlit 的 Opaque/Masked，保留镜像绕序和原 PSO 状态；透明仍使用 CPU 排序/普通 Forward，光栅阴影保留相机外的 CPU 投影。其他 Shader 的不透明表面显式拒绝，不自动改成 Lit。RayTracing 组合可使用相同索引 GBuffer，AS/反射保持自己的支持约束。

最大 16384 个索引实例、64 个图像/色彩空间版本；超限明确报错。表从独立的有界上传页（8 MiB × 最多 8 页）生成，成功提交后封存；结构化 stride 与偏移对齐由 RHI 验证。CPU 间接参数通过 Graph Copy 写入 Device Buffer，然后声明 Indirect Read，正常渲染不等待/读回 Count。原 `UploadStatistics()` 统计普通帧上传池，索引表页计入 Runtime 总内存账本。

场景文档另有 10000 对象限制；模型展开后的 primitive 实例数可能大于对象数。索引容量按展开后的实例检查。材质输入/策略在每帧首次读取后冻结，同材质、Pass、绕序的索引准备复用一份模板；Pipeline 替换每帧按原 PSO 身份处理一次。CPU 提取用本次文档的对象索引，避免逐实例线性搜索；不引入持久可编辑缓存或另一份材质权威。

## GPU 视锥剔除

GPU 模式从 allInstances 提取表，跳过 CPU 相机剔除；CPU 索引基线复用 Extractor 已计算的可见身份，不重复执行同一包围盒测试。透明单独保留 CPU 可见性与排序，光栅阴影保持独立候选集。

每个 Pipeline/Queue/Stencil 批次按全部候选数预留连续范围。`Indexed.ClearCounts` 写零，`Indexed.FrustumCulling` 读取该版本后写出压缩列表/Args/Count；Raster 显式读可见表为 ShaderRead、参数和 Count 为 Indirect。Graph 处理 Storage→Storage 和 Storage→Indirect/ShaderRead 的依赖。空场景不派发零尺寸 Dispatch；全剔除仍执行有界批次，GPU Count=0。

视锥测试与 CPU 相同，判断世界 AABB 八角是否同时处于任一裁剪面外。一个 64 线程组负责一个批次，按 64 候选分块做稳定前缀压缩，保持原实例顺序；没有 CPU 动态 Count 循环、Hi-Z、LOD 或原子截断后的不确定子集。容量等于全部候选数，合法输入不会溢出；超过 16384 个展开实例在写 GPU 缓冲前明确拒绝。单组分块限制并行度，GPU 剔除不保证比 CPU 更快。

GPU 模式的 Snapshot.cpuVisibilityKnown=false，instances/culledInstances 不提供 CPU 可见结果；allInstances 和阴影候选仍有效。报告 visible=null、visibilitySource=gpu，不能把来源数当成 GPU 可见数。显式 cullingDiagnostics 才增加 Count/可见索引读回，SceneReadback.culling 提供范围元数据；调用者等待真实提交完成后再映射，诊断不控制本帧绘制。

## 验证入口

`gpu.dx12.indirect` / `gpu.vulkan.indirect` 使用同一真实 GPU Shader，比较直接、CPU 参数与 Compute 参数的完整 RGBA8 图像。覆盖索引/非索引、多条固定命令、Count 0/1/2/3/99 截断、非零 firstVertex/firstIndex/vertexOffset/firstInstance、跨命令实例身份和单命令多实例；同时检查偏移/容量/能力拒绝、未声明 Count、取消与提交退休后的资源引用。测试要求实际设备支持这组可选能力，不静默降级。

`gpu.dx12.descriptor_array` / `gpu.vulkan.descriptor_array` 在同一 64 线程组中打乱访问 64 个数组元素，检查跨 space 映射、完整表与两套在途不可变表的独立结果、旧纹理保活/退休、每个元素的 Graph 访问、缺项/重复/越界/空 View/布局重叠拒绝，以及描述符计数和销毁后资源账本。

`gpu.dx12.indexed_scene` / `gpu.vulkan.indexed_scene` 对比原材质路径、索引直接与 CPU 间接结果，覆盖 PBR、Masked/透明、镜像、奇数尺寸、空场景/全剔除和两个在途版本。CPU 索引直接与间接要求逐字节一致；与原材质路径的比较另外检查最大误差及 MSE。验证状态以计划中的实际结果为准。

该专项还覆盖 GPU Count/可见列表逐项对照，跨 64 线程边界的 1/64/65/1024/4096 候选、稳定顺序、16384 容量拒绝，以及禁用 CPU 相机剔除的契约。固定 ID/位置的规模场景输出到测试目录的 Pbr/scale-{64,1024,4096}，供共享 Benchmark 配置复用。实际数据与边界见 [GPU Driven 测量](performance/m6-gpu-driven.md)。
