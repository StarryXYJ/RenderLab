# M1 最小双后端 RHI

公共接口按下表组织，可运行的 API 使用示例位于 `src/samples/rhi_demo.cpp`，构建与复现见 [构建与运行](../manual/build.md)。本层不依赖 Shader 资产系统、Render Graph 或 Renderer。

## 文件职责与依赖

| 入口 | 职责 |
| --- | --- |
| `src/rhi/resources.hpp` | 资源/程序/绑定描述、对象所有权与内存账本；不包含窗口、命令或 Runtime |
| `src/rhi/commands.hpp` | 拥有资源引用的命令包与一次性录制，参数/录制边界检查在 `commands.cpp` |
| `src/rhi/runtime.hpp` | 创建设备对象的接口、真实提交完成点、等待与回收；前向声明 CommandList，析构在实现文件定义 |
| `src/rhi/swapchain.hpp` | 图像获取、展示与重建契约 |
| `src/rhi/upload.hpp` | 固定容量 Arena、按完成点复用的上传环及行间距约定 |

调用方直接包含使用的头文件，`runtime.hpp` 不作为转发全部 API 的总头。公共录制、上传和 Runtime 的参数检查复用 `detail/validation.hpp`；资源账本和队列提交仍各有唯一所有者。

DX12/Vulkan 后端采用相同文件边界：`*_device.cpp` 做设备发现与能力查询；`*_runtime.hpp/.cpp` 声明后端 Runtime 并维护队列/完成点；`*_resources.cpp` 创建和分配资源，`*_pipeline.cpp` 创建布局、绑定与管线，`*_commands.cpp` 将命令包翻译为原生命令，`*_swapchain.cpp` 管理展示生命周期。`*_objects.hpp` 仅供后端共享原生对象与 RAII，`*_common.hpp/.cpp` 统一格式/状态翻译和错误检查。后端类型位于各自命名空间，上层通过 Runtime 使用，不访问这些内部头。

一个后端仍只有一个 Runtime 类，相关方法在对应职责的 `.cpp` 中以类外定义实现；每个 `.cpp` 独立编译，不包含其他 `.cpp` 或依赖文本拼接的 partial/inl 片段。没有新增 Manager、重复状态机或单独的队列/资源所有者。

样例与验收分别组织：`rhi_demo.cpp` 准备和录制场景；`rhi_acceptance.cpp` 的 `VerifyRhi()` 读取场景输出并核验图像/查询；`rhi_contract_checks.cpp` 核验传输、保活和上传复用。场景通过 `GetNoiseTexture()` 提供处于 Sampled 状态的借用输出，验收执行后恢复该状态。Shader 文件读取位于 `samples/shader_io.cpp`，RHI 只接收已编译字节和入口名。Sandbox 的 `main.cpp` 负责启动、参数与报告，`presentation.cpp` 负责窗口事件、FrameContext 和展示循环。

## 设备、对象与提交

`Device::CreateRuntime()` 在已选设备上创建一条 Graphics Queue 的运行上下文；后端选择仅发生在设备创建时。所有 Runtime 调用、对象创建、录制和提交限于创建线程。首期支持一个 Device 和一个 Runtime；尚不支持多个 Vulkan 设备共享 volk 全局 dispatch。

资源、View、Sampler、BindingLayout、BindingGroup、ShaderModule 和 Pipeline 采用 RAII。描述和绑定快照构造后只读。View 保活 Texture，Group 保活 Layout、View 与 Sampler，Pipeline 保活 Layout 和 ShaderModule。GPU 对象不可自行构造或跨 Runtime 使用；调用者必须在 Runtime 和 Device 关闭前释放所有对象。

`CommandList` 是一次性、拥有数据的命令序列。录制时立即捕获对象的强引用，应用可以在提交前释放自己的引用。`Cancel()` 或丢弃未提交列表释放这些引用，不产生完成点。提交后列表转移给 Runtime，只有真实图形提交完成后才回收列表及其依赖。公共层检查跨 Runtime 使用、拷贝边界、行对齐、渲染/Dispatch 前置条件、附件格式、Marker 和 Timestamp 配对。

`Submit()` 返回单队列递增完成点；调用者只在提交成功后发布持久资源的最终状态和完成点。`Wait(point)` 不能等待尚未提交的点；`Collect()` 非阻塞回收。最多允许八个未回收提交，超过容量要求先等待/回收。原生命令上下文与 QueryPool 按完成点复用，DX12 重置已完成的 CommandAllocator/CommandList，Vulkan 重置已完成的 CommandPool。不存在全局猜测状态机。

FrameContext 的索引由 CPU 帧序号选择，与交换链图像索引独立。样例使用两个 FrameContext 和后端实际交换链图像数；每个 FrameContext 按自身完成点复用深度附件。交换链图像及其 View 按获取的图像索引缓存。

展示的生命周期需要单独处理：DX12 的 Present 位于图形提交信号之后，resize/销毁额外等待其队列尾部；Vulkan 的 render-finished 二进制信号量属于交换链图像，获取信号量属于按 Timeline 完成点复用的 CPU 槽。Vulkan 在 resize/销毁等待展示队列，并在 Acquire 等待范围内执行图像布局转换。正常帧仅在复用对应 FrameContext/获取槽时等待，不调用 DeviceIdle 或 QueueIdle。

## 资源、内存与屏障

M1 仅实现当前用到的 2D、单 mip、单 layer、单采样纹理：RGBA8/BGRA8 UNORM、对应 sRGB 格式和 D32。Storage 纹理限定 RGBA8_UNORM；M2 补充常量缓冲、只读与可写结构化缓冲，Binding 使用固定 space 0..3。管线显式声明单顶点流的 stride、semantic、location、float2/3/4 和 offset，RHI 不依赖样例的顶点结构。样例在自身模块定义 36 字节 position/color/uv，索引为 uint32。绑定图形管线后再绑定 Geometry；切换管线后重新绑定 Group 和 Geometry。

Buffer 的内存类别为 Device、Upload、Readback。仅后两类允许映射；读回前必须完成 CopyDest→HostRead 屏障并等待提交。Upload 的映射内容在消费它的提交完成前不得改写。Vulkan 首版要求 HostVisible + HostCoherent 内存类型，不引入隐藏的非一致内存刷新行为。设备资源初始状态为 Undefined，Upload 为 HostWrite，Readback 为 CopyDest。

首版分配器采用 DX12 committed resource 和 Vulkan dedicated allocation，不引入尚无子分配需求的 VMA/D3D12MA 包装。AllocationLedger 记录实际原生分配大小、当前占用与高水位，共享 256 MiB 明确预算，超额报错。交换链借用图像、驱动内部分配和描述符堆不计入该数值，它不是完整显存预算报告。普通资源随安全完成点释放；命令池保持有界的最高并发数量至 Runtime 关闭。

`UploadArena` 是固定容量、显式对齐的线性上传分配器，超容量报错，不静默增长。`UploadRing` 轮换两个 Arena，BeginFrame 等待该段完成点后 Reset，EndFrame 使用实际消费该段的提交点封存。不要保留 Arena 引用并在封存后继续写入；需要多次提交消费同一段时，用最后一次提交点封存。

纹理 Copy 的公共对齐约定为行间距 256 字节、Buffer offset 512 字节。`TextureRowPitch(width)` 提供共同布局；Vulkan 把字节 pitch 转成 texel row length。有效像素外的行 padding 未定义，测试只对有效像素比对。

`State` 是首版保守的用途、访问、阶段与布局预设，覆盖 Copy、Vertex、Index、Indirect、Compute Storage、Fragment/Compute Sampled、Color、Depth 和 Present。屏障由调用者明确录制，RHI 不推断前态、不替调用者自动切换。Storage→Storage 在 DX12 发出 UAV barrier，在 Vulkan 发出 Synchronization2 内存依赖，即使布局未变也不能省略。M2 已补充 UniformRead、ShaderRead 和 Storage Buffer；HostWrite→UniformRead/ShaderRead 显式描述上传缓冲供 Shader 读取的依赖。仍不实现子资源细分、多队列与 Ownership Transfer；后续图层应生成明确计划，而非依赖本层猜测。

## 程序与绑定

测试 Shader 在构建时由同一个 DXC 编译为 DXIL SM 6.0 和 Vulkan 1.3 SPIR-V。ShaderModule 持有已编译字节与显式入口名，不调用编译器；本层不实现元数据、反射、缓存或热重载，这些由 [M2 Shader 层](m2-shader-material.md) 负责。样例选择 VSMain/PSMain/CSMain，后端不写死这些名称。

BindingLayout 显式列出 set/space 0 的逻辑 slot 和资源类型，Group 必须完全匹配且不可修改。DX12 为每项创建 descriptor table，并在 Group 中分配独立可见堆；Vulkan 使用 DescriptorSetLayout/DescriptorPool/DescriptorSet。Shader 通过 `vk::binding` 和相同编号的 HLSL register 明确映射，没有后端分支泄漏到样例命令中。首版绑定规模很小，未建立 Bindless、全局描述符索引或通用注册表。

PSO 在样例准备阶段创建，录制只绑定。Direct Draw、Indexed Draw、Dispatch 均已实现；`DrawArguments` 是公共 16 字节非索引间接参数格式，样例用一次 DrawIndirect 验证。尚未启用 Indirect Count、非零 firstInstance、多绘制、GPU 剔除或光追；设备能力报告中的支持与已实现路径不同。

## 坐标、颜色与诊断

样例采用右手、Y 向上的约定，clip depth 为 0..1，Reversed-Z 清零并使用 GreaterEqual。DX12 正面为 clip 空间 CCW；Vulkan 在后端集中使用负高度 Viewport，HLSL 不增加 Y 翻转。纹理和读回原点为左上，UV 向下。矩阵使用列向量，测试 Shader 的非对称平移矩阵与 CPU 读回坐标比对；CPU 矩阵缓冲布局和真实相机投影留待对应数据接口实现。

颜色计算和混合使用线性值，由 sRGB 附件在输出时编码。噪声纹理为线性 RGBA8_UNORM 数据；其原始读回必须逐字节匹配，而屏幕样例包含显示编码。测试条把线性 0.25、alpha 0.5 与线性背景 0.02 混合，读回约 103/255，防止误在线性输出前手动编码。

原生资源和管线具有名称；Marker 在 DX12 使用官方 WinPixEventRuntime，Vulkan 使用 DebugUtils。Timestamp 每个列表有两个 slot，读回包含原始 ticks，Runtime 提供纳秒周期；Vulkan 当前要求该队列有 64 个有效时间戳位。验证层开启时的计时仅用于证明查询链路，不作为性能结论。Vulkan 验证同时启用 synchronization validation；不屏蔽同步和生命周期错误。设备丢失给出原生错误并退出，不自动重建设备。

## 验收入口与边界

`--rhi-test` 执行确定性验收，保存 `noise.rgba`、`noise.bmp` 和 `triangle-noise.bmp`：

- Buffer 全字节往返，纹理宽 1/63/64/65/257、奇数行数、非零上传 offset。
- 固定种子 257×131 Compute 初始化与第二次 Storage 读改写，Storage→Storage 及写后采样依赖；全部 33,667 个像素与 CPU 整数参考逐字节比对。
- 录制保活、提前释放应用引用、取消、延迟提交、提交完成回收，以及两段上传区跨六次提交的覆盖检查。
- 索引三角形、顶点属性、绕序、Y/UV 原点、平移、Reversed-Z、线性混合、采样和间接参数的像素探针。
- Timestamp 读回；场景退出后账本占用必须归零。

`--window-test` 在实际呈现过程中测试十次交换链尺寸/暂停变化，含两轮最小化与恢复；`--frames 1` 测试提交后立即退出。CTest 对两个后端均执行上述路径。截图/报告/日志留在忽略的构建树，固定测试与 Shader 源码保留在仓库。

这些结果验证最小 RHI，不代表已有通用 Renderer、Shader 材质系统、完整数学库或高级 GPU 功能。状态和实际构建/硬件结果只记录于 [阶段计划](../design/phaseplan.md)。
