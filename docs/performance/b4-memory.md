# B4 GPU 内存验证

日期：2026 年 10 月 30 日。状态只登记于 [阶段计划](../../design/phaseplan.md)，
口径见 [GPU 内存](../gpu-memory.md)，复现见 [手册](../../manual/build.md#gpu-内存诊断)。

## 计费基础

Windows x64、MSVC 19.51.36257、NMake，RTX 5060；两个配置均执行 `--clean-first` 全目标构建，
避免共享 MemoryStats/Device 接口修改后混入旧目标文件。

Release 完整 CTest 66/66。Debug 首轮 65/66，仅 Vulkan 光追窗口在 90 秒超时，
未发现验证错误；该用例随后独立连续三次通过（约 2.14/2.13/2.11 秒），没有修改测试时限或
生产窗口代码。首轮原因未确认，不把复验写成首次全绿或声称已修复窗口问题。
余项覆盖双后端 RHI、Alias、多队列/并行、AS、光追、资源/Shader、展示与关闭。
Python Benchmark 15/15，托管两配置各 22/22。

工作台 Release/DX12、Debug/Vulkan 各 23 项自动检查通过，含十轮双向后端切换和启动中关闭。
各 30 份正常 Session 关闭报告的 RemainingBytes/ValidationErrors/WorkerFailed 为零；
对应日志新增 reserved/allocations/blocks 也全部为零。未重做长期压力、其他 GPU 或人工混合 DPI。

固定 PBR 640×360，预热 8 帧、测量 8 帧、额外一帧截图，两后端图像 hash 与 B4.3 基线一致。
以下是截图前一次资源账本快照，不是稳定性能收益：

| 后端 | used / reserved 字节 | allocation / block count | driver 分组 |
| --- | ---: | ---: | --- |
| DX12 | 7602176 / 7602176 | 26 / 26 | local、nonLocal |
| Vulkan | 6366016 / 6366016 | 26 / 26 | heap 0、1、2 |

本机分组 usage/budget 均实际查询到。DXGI local usage 为 19935232 字节，Vulkan heap 0
usage 为 63594496 字节，均不等于各自 RHI 账本；不能把差值称为“泄漏”，或跨后端相减作同口径比较。
过程同时运行过其他 GPU 测试，driver budget 可受系统负载影响，进程 working set/private commit
又在报告保存时采集，不是同一时刻的原子物理内存分解。两组探针关闭均观察到四项当前计数归零，
validation errors=0，已有 optimized-clear / 顶点未消费警告保留。

新增 CPU 合约覆盖空闲 backing 仍占预算、范围复用不重复收取 block、原生独占与模拟子分配混用、
拒绝不改当前与峰值、UINT64 边界。实际 Alias 用例验证一个组仅一个 range/block，
预算不足时既不漏计也不留下残余 charge。子分配仅为 CPU 账本模拟，生产资源此时仍是
DX12 committed / Vulkan dedicated，不能将本轮写成已经采用 D3D12MA/VMA 或降低了实际块数。

证据目录：`out/b4/memory-accounting` 的构建/CTest 日志、隔离重试日志、`probe-*` 报告/图像、
`managed-tests` TRX 与 `editor-{Release-dx12,Debug-vulkan}` 的自检和关闭报告。
