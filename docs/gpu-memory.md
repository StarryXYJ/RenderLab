# GPU 内存计费与诊断

本文件定义 RHI 的计费契约。实施状态只见 [阶段计划](../design/phaseplan.md)，
复现命令见 [构建手册](../manual/build.md#gpu-内存诊断)。

## 四种口径

| 数据 | 含义 | 不表示 |
| --- | --- | --- |
| `MemoryStats.used/highWater` | 已获准的分配范围当前/峰值字节，含原生要求的对齐；Alias 组共享一次 charge | 请求 payload、逻辑 Texture 总大小或完整显存用量 |
| `reserved/reservedHighWater` | 这些范围的底层 backing 当前/峰值保留字节，包含空闲块 | 已使用的 payload 或驱动的可用显存 |
| `Device::QueryMemoryBudget()` | 驱动对当前进程在原生 segment/heap 的 usage 与 budget 估计 | 应用硬限额、精确 RHI 资源合计或跨后端同一物理口径 |
| process working set / private bytes | Win32 当前进程驻留工作集 / private commit | GPU 原生分配、单独的 Renderer 内存或可与前两项直接相加的数据 |

`allocations/allocationHighWater` 是 charge 的范围数，`blocks/blockHighWater` 是 backing 数；
不等于逻辑资源对象数、Draw 数或原生创建 API 调用数。一个 Alias 组计一个范围和一个共享块，
而非每个 Texture 各计一次。普通 committed/dedicated 分配当前一个范围对应一个块，二者字节相同。
Host 可见上传/读回同样计入。Swapchain 借用图像、descriptor heap/pool、query/驱动内部存储
不在这一 Buffer/Texture/backing 账本内；不能把它写成全进程 VRAM。

## 预算与生命周期

`Runtime::SetAllocationBudget(bytes)` 是单 Runtime 的应用硬上限，覆盖 **reserved backing**；
资源范围必须装入已保留的 backing，因此满足 `used <= reserved <= budget`。
它不预留内存、不保证驱动能成功分配、不要求 usage 小于实时 driver budget，也不执行自动驻留。
即使 `used=0`，未释放的 backing 仍占预算，不能把 limit 降到 reserved 以下。
Renderer/Graph 的缓存目标继续使用此应用配置，估算字节不能替换真实 backing charge。

`AllocationLedger` 只由既有 RHI owner-thread 生命周期调用，不是可跨线程并发写的分配器：

- `ReserveBlock/ReleaseBlock` 管理 backing；`AddAllocation/RemoveAllocation` 管理其范围。
- `Add/Remove` 是一个独占范围与一个同尺寸块的组合，继续供现有两个后端使用。
- 普通分配与原生 Alias 使用同一账本；共享 backing 的最后一个所有者负责归还 charge。
- 预算拒绝在修改字节、计数和峰值之前发生；native 创建在已获准 reservation 之后失败时，
  由 RAII 归还当前 charge。峰值保留已获准但 native 创建随后失败的 reservation，不把峰值当成功创建量。
- 已录制/已提交命令、View、Pool 和完成点的保活规则不变。取消释放 CPU 引用，
  退休需要实际完成点；资源或结果仍被持有时，不能仅因 GPU 完成就隐去占用。
- 非匹配释放是内部生命周期错误，`noexcept` 释放路径终止而不允许无符号下溢伪装成巨大或零占用。

未来子分配器应分别计范围和块，空闲块仍可观察。关闭检查必须同时观察 allocated、reserved、
allocation count 和 block count，不能只因资源范围归零就宣称已释放全部 backing。
计费基础不等于已完成 D3D12MA/VMA 验收。没有 defragmentation 或资源搬迁。

`Runtime::TrimMemory()` 是 owner-thread 显式入口，不等待 GPU，不移动资源，只请求回收完全
空闲的 backing 池；默认 committed/dedicated 路径无此类缓存，调用为空操作。
录制或在途对象仍保有范围，不可因请求 Trim 被退休。后台复用的上传/描述符/Graph 池不因此
被当成无引用，也不改变它们自己的回收条件。

### 可选 DX12 试验

`RENDERLAB_DX12_MEMORY_ALLOCATOR` 默认关闭，正常构建不获取或链接该依赖。
开启时使用固定 D3D12MA 3.1.0，具体适配器位于 `src/rhi/dx12/dx12_memory.cpp/.hpp`。
代码为 B4.5b 检查点，其验收和暂停状态只见阶段计划，不将已有实现写成正式完成能力。
试验设计采用按 heap/用途分开的 16 MiB 固定池、先申请已有块、增长前账本准入、
小预算/大资源 committed 路径以及只回收完全空闲池。tight alignment 与小 Buffer 的 committed
偏好在试验中关闭，以保留当前 RHI 对齐和共享块目标；线程边界仍是既有 owner-thread。
它不替代上传/描述符分配器或原生 Graph Alias 分配。
未完成 ON 的两配置整体验收、真实失败/关闭回归、成对测量和安装检查前，不默认启用。

## 驱动与进程采样

`QueryMemoryBudget()` 是显式、owner-thread 的诊断调用，返回拥有数据的值快照。
DX12 查询 node 0 的 `IDXGIAdapter3::QueryVideoMemoryInfo`，分别命名 `dxgi.local/nonLocal`；
Vulkan 检查并启用可选 `VK_EXT_memory_budget`，逐原生 heap 命名 `vulkan.heap.N`，
保存 device-local 分类，不重复把一个 heap 按其 memory type 加总。
这两个原生分组不保证一一对应，不能直接把某个编号视作同一堆，也不把 device-local 等同独立显存。

查询不可用、单 segment 查询失败或设备已关闭时，不用静态 Adapter 容量猜测动态预算：
整体未知导出 `driverBudgets: null`，单项未知用 null，真实零值保留为零。
关闭后的查询返回空结果，不访问已销毁的原生对象。预算可随系统负载变化，并非刚性分配保证。

Headless 的资源/驱动快照在测量结束、额外截图之前采集；进程数据仍在报告保存时采集。
窗口报告在 presentation 结束并等待后采集。它们不是原子快照或同一 GPU 完成帧的数值，
也不计入 Benchmark 测量帧。`GetMemoryStats()` 只复制 CPU 账本，不新增 OS/driver 查询。

报告保留 `usedBytes/highWaterBytes/budgetBytes`，新增 reserved、allocation/block count 与峰值、
`budgetScope` 和 `driverBudgets`。单位均为字节或明确的数量。工作台 v10 C ABI 不变，
`native_bytes/peak_bytes` 仍是 tracked allocation；按需诊断 JSON 附加 reserved 与计数，
关闭日志附加 backing/count。未采集驱动数据的面板不因有这些计数就显示虚构的 driver budget。

## 验证入口

- `memory.contracts`：独占/子分配/混合计费、空闲块、复用、拒绝不变、峰值、UINT64 边界。
- `rendergraph.contracts`：owner-thread、配置预算不能低于空闲 backing、统一拒绝信息。
- `gpu.{dx12,vulkan}.alias`：普通纹理与共享 backing 的字节/数量、预算失败不变、最后引用保活、
  驱动 segment 查询及关闭后未知；保留连续帧与取消的像素对照。
- `gpu.{dx12,vulkan}.rhi`：取消与真实提交完成之后检查范围和 backing 的字节/数量。
- Sandbox/Player 关闭日志：同时记录 allocated/reserved/allocations/blocks，非零则失败。

原生语义依据：
[DXGI 内存信息](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info)、
[Vulkan heap budget](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryBudgetPropertiesEXT.html)、
[进程内存计数](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)。
