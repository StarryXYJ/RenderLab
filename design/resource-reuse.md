# B3：逐帧资源复用方案

分析日期：2026 年 10 月 26 日。本文是基于现有代码和 [B2 实测](../docs/performance/b2-pbr-rtx5060.md) 的实施建议，未包含优化后的性能结果。实施状态仅记录在 [阶段计划](phaseplan.md)。

## 优化目标与依据

先减少稳定场景每帧的原生资源创建和销毁，保持现有 Shader、Pass、渲染质量与单队列语义。CPU 数据组织和后端分配策略分别优化，上层仍通过同一 RHI 接口绑定。

B2 的长基线为 DX12/Vulkan 串行帧均值 13.4026/5.6008ms，其中 CPU render 为 9.1965/2.4398ms；CPU render 差距约占完整帧差距的 87%。GPU Pass 合计约 0.04ms，但不是完整 GPU 帧时间。诊断中完整 PBR 每帧创建 23 个 Buffer、10 个 BindingGroup；典型 Buffer 创建为 4.59/1.32ms，BindingGroup 创建为 2.87/0.03ms。诊断数值使用三次运行均值的中位数，不能与长基线混合相加来承诺收益。

当前高成本路径：

| 路径 | 当前行为 | 优化方向 |
| --- | --- | --- |
| SceneRenderer::ParameterBuffer | 每帧为 View、Lighting、Shadow 和 Object 独立建 Upload Buffer | 完成点和引用共同保护的上传页，按偏移绑定 |
| Material Runtime::Prepare | 每次准备重新打包、上传材质常量 | 按包代际及内容复用不可变常量 |
| DX12 CreateGroup | 每组创建资源/采样器 shader-visible heap | 长期持有 heap，组只占其中的不可变区间 |
| Vulkan CreateGroup | 每组创建 DescriptorPool，分配四个 set | 后续复用 pool/set 分配，保留同一绑定语义 |
| Graph::Prepare | 每图创建 timestamp 读回和 TextureView | 受结果持有期保护的读回池、物理纹理关联的 View 复用 |
| Runtime::Collect | 退休命令包时集中释放上述原生对象 | 移除反复分配的根源，再检查剩余回收成本 |

CPU 探针是墙钟区间，包含驱动和系统停顿。Collect 计时也不覆盖所有局部析构。没有证据支持直接将剩余成本归咎于某个驱动内部函数。

## 数据按生命周期分开

| 数据 | 所有者与写入时机 | 复用条件 |
| --- | --- | --- |
| View/Object/Lighting/Shadow/Display 常量 | Renderer 的本次帧准备，录制前完成写入 | 使用它的提交已完成，且没有 CPU 快照或录制命令继续持有 |
| Material 常量 | 包代际下的不可变参数版本，内容变化才新建 | 旧版本仍可被 Prepared/LiveMaterial 保留和再次录制 |
| 描述符区间 | BindingGroup 的不可变后端分配 | 组不再被 CPU 或命令引用，且相关提交已完成 |
| Timestamp 读回 | 提交结果的 profiling 快照 | GPU 已完成，结果及读取者均已释放 |
| Graph 纹理和 View | 资源池中的物理分配 | 图租约、View、绑定组和 GPU 引用均允许安全复用 |

只检查 GPU Fence 不足以保护已录制未提交的命令，也不足以保护以后还会使用的旧 CPU 快照。固定两帧取模不能作为通用回收条件。

## B3.1 帧上传页

复用现有 UploadArena 的写入和高水位机制，但不直接将现有 UploadRing 作为通用快照存储。UploadRing::BeginFrame 目前只等待提交完成后 Reset，缺少取消接口，也不检查旧 Prepared 是否仍引用该页。

建议先在 Renderer 使用小型、有界的整页复用池：每帧从可用页取得 arena，返回 Buffer、offset、size；用现有 rhi::Binding 范围传递，不改变 HLSL 参数契约。稳定场景预热后复用页，容量不足时按页扩展；配置容量上限并记录在用量、高水位和扩容次数。

页的可用判断必须同时满足完成点和外部引用条件。当前 Binding、Graph 闭包和 CommandList 会持有 shared_ptr<Buffer>，首版可以像现有 Graph Pool 一样利用这些引用保护整页；池内部引用数量必须集中定义，避免零散的 use_count 魔数。无需为每次小切片建立全局资源身份，也不复用仍被外部引用的页的局部空间。后续若引入独立 slice lease，必须让绑定和命令保活它，仅给 arena 增加一个 lease 字段不够。

一页只存本帧先写后读的常量，不混合 UAV、CopyDest 和并发 CPU 更新。所有写入完成后再录制；同一 backing Buffer 在一张图中只 Import 一次，各 Draw 声明并绑定实际范围。当前 Graph 整 Buffer 状态跟踪可以保留，不必提前建设区间调度。

具体接入还需修正两个 offset=0 假设：SceneRenderer 的 bind helper 目前绑定整个 Buffer；Display 回调也固定传 0。两处都要保留 slice 的 offset/size，满足公共 RHI 的 256 字节偏移和大小对齐，以及 64KiB 常量范围限制。对齐仍须满足设备限制。

提交成功才登记实际 completion。准备/录制异常或取消时释放本次拥有权，不伪造完成点；旧帧和未提交命令仍通过引用保护页面。页面满且达到预算时给出明确诊断，不能对尚未提交的使用调用 Wait。

预期：先移除帧/对象常量的独立 Buffer 分配，两后端共同受益。此步还保留材质和 timestamp 的分配，不能提前宣称全部 23 次创建归零。

## B3.2 不可变材质常量

在材质 GPU 代际内缓存打包后的常量版本，避免把包含 View/Object/阴影资源的整个 Prepared 按材质 ID 缓存。Prepared 中外部数据每帧变化，不能作为静态材质缓存的命中结果。

首版以“后端/包代际、实际 block 布局身份、完整打包字节”确定常量版本；hash 只作索引，命中后核对布局和字节。打包 padding 必须确定。不同材质身份但相同字节可以共享同一不可变常量；资源 symbol 和文件修改时间不能独自承担缓存身份。纹理/采样器仍由显式资源解析器提供，不因数值缓存命中跳过资源更新。

优先缓存现有独立常量 Buffer，先减少重复创建。若变化频繁时新版本分配仍占显著成本，再用具有相同不可变拥有权的持久页分配；不强迫长期材质进入帧 arena。缓存保留有界的强引用，以免串行 headless 在帧间释放后每次重新分配；淘汰只释放缓存引用，不能让仍被旧快照持有的数据可写或失效。

普通数值编辑产生新常量版本；静态轴、Pass 状态和 PSO 选择继续由现有契约处理。包热重载保持整代发布，旧 Prepared 可继续使用旧包和数据。资源初始/最终状态仍通过现有导入和成功提交协议交接，不能依据 Memory::Upload 推断每次都有新的 HostWrite，也不能在首次提交失败时把状态标成已执行。

预期：静态场景预热后不再为未修改材质创建常量 Buffer。字段打包和迁移的 CPU 缓存只有在测量仍显著时再细化，避免为每种状态再建一套权威材质对象。

## B3.3 描述符分配复用

DX12 继续保留 BindingLayout/BindingGroup 与当前 Root Signature 映射。由后端持有有界的 shader-visible 资源 heap 和 sampler heap 页；CreateGroup 从页取得区间并写入描述符，DxGroup 保留页和区间的拥有权，命令照常保活组及其资源。首版可保持现有每个 root table 对应一项描述符，不需要同步修改 Root Signature 结构。

区间在组的最后一个安全引用释放后才回收。现有 Runtime 的待完成命令包可以保护 GPU 使用和已录制未提交使用；分配器还要保证异常构造归还已取得的区间，并使分配状态的寿命覆盖存活的组。不能按 CPU 帧边界重写所有描述符。

优先采用稳定页和空闲区间分配，减少 heap 创建/释放。发生扩页时仍满足每次绑定最多一个资源 heap 和一个 sampler heap；一个组的同类描述符必须落在同一页。切换 heap 后重新设置所需 root tables，不能跳过必要绑定。首版不跨页搬迁已发布区间；记录页数、空闲/占用、高水位、扩页和实际 SetDescriptorHeaps 次数，以判断后续碎片和切换成本。采样器页容量必须检查设备/API 限制，不能默认可以无限扩一个 heap。

此步允许每帧仍创建 10 个轻量逻辑 BindingGroup，目标是预热后不再每帧创建 19 个原生 heap。先不建立包含所有 Buffer/Texture 的强引用组缓存：它可能长期钉住上传页和 Graph 临时纹理，使已有 Pool::Available 永远失败。

Vulkan 随后将每组新建 pool 改为有界 pool 分配或安全复用 set，释放/Reset 同样受实际引用和完成点约束。不能只因 Fence 完成便 Reset 整个 pool，池内可能还有以后会重新提交的 CPU BindingGroup。保留当前普通 UniformBuffer 和显式 offset，不必为本轮引入 dynamic offsets、Bindless 或 update-after-bind。

## B3.4 读回、View 与剩余回收

Timestamp 采用固定上限容量的读回池。现有最大 64 个 slot，单 Buffer 的有效 tick 数据仅 512 字节；关键是复用原生分配，不是缩减这点数据量。GpuProfile 持有 ticks，Read 可以在 GPU 完成后被调用多次，因此必须同时等待 completion 和 CPU 结果释放才复用。支持双帧在途、空图、异常和延后读取。

Graph 已池化 Texture，但 Prepare 每次重新 CreateView。建议将默认 View 与物理 Allocation 关联，并补充外部 target 的有界 View 复用。增加强引用 View 时要调整可用条件：View 本身持有 Texture，现有 texture.use_count()==1 不能原样保留；还必须检查 View 是否被外部组/命令持有。不要让 Texture 和 View 互相强持有形成环。resize/trim 仅移除安全资源，旧展示资源仍按原有协议退休。

上述复用完成后重测 Collect 的时间和长尾，再细分命令包析构、实际资源销毁和 pool 维护。不要先把析构挪出计时区或后台线程来制造收益。D3D12MA/VMA 后续可降低一般资源分配成本，但不能替代常量复用，也不会消除每帧创建 heap/对象的全部成本。

## 验证与收益判定

每个子阶段独立提交，先验证画面和生命周期，再测相同配置性能。实施验收需要：

- 双后端 Debug/Validation 和 Release 既有回归；PBR、原小场景、HDR/IBL/阴影/透明及静态状态不变。对照同一后端优化前后画面；跨后端继续采用现有容差。
- 保留旧 Prepared、CPU BindingGroup、已录制未提交命令跨越多次帧复用；再提交仍读到旧值。编辑、热重载、缓存淘汰和异常取消不能覆盖旧数据。
- 双帧在途、resize、页面/描述符容量不足、延迟读取 profiling 与资源池 Trim；长期循环后资源占用有界，只有实际安全的页和区间可复用。
- 原基线二进制和 raw 数据继续保留。每阶段输出独立目录；Release/validation off、CPU profiler off、同场景/分辨率/质量/预热及测量时间，后端顺序交替。不在 GPU 测量期间并发构建或运行另一套 GPU 测试。
- 中间阶段先做较短的配对测量确认方向，最终沿用每后端 8 次、5 秒预热/20 秒测量的长基线。另用 profiler-on 诊断分配来源，两者不混合聚合；所有异常样本保留并报告。
- 报告完整帧、CPU render、原生 fence wait、retire、P50/P95/P99、重复间波动和内存高水位。新增原生页/heap/pool 创建与扩容计数，区分逻辑 CreateGroup 次数和真正的原生创建。

结构验收目标是稳定 PBR 预热后消除常量、timestamp 和描述符 heap/pool 的持续原生分配；场景变化、扩容和热重载允许受预算约束的分配。耗时收益以配对实测确认，不能从 B2 区间简单相减推导“必到某个 FPS”。

保留当前串行 benchmark 测量口径，先比较单帧 CPU 成本。窗口流水吞吐或新增在途 benchmark 属于另一种模式，需独立标注，不能通过移除每帧等待改变基线含义。

## 暂不扩大的范围

Graph compile 当前约 0.03ms，先不做执行计划缓存；GPU PBR 计算尚无主要瓶颈证据，先不改 BRDF、降低质量或改渲染算法。多线程录制、多队列和 Bindless 留在已有后续阶段。优先顺序为上传页、不可变材质常量、描述符分配、读回/View 收尾；每步重新测量后调整下一步优先级。
