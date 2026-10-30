# B4 基础库与计算/分配测量

日期：2026 年 10 月 30 日。实施状态只登记于 [阶段计划](../../design/phaseplan.md)。
指标和计时入口见 [Benchmark](../benchmark.md#场景准备分解与-cpu-kernel)，运行见 [手册](../../manual/build.md#cpu-与基础优化基线)。

## 原始基线

本机 Windows x64、MSVC 19.51.36257，Release。GPU 为 RTX 5060，DX12 驱动 32.0.15.9174，
Vulkan 591.74。构建树 `out/build/m5-release`，Debug 为 `out/build/m5-nmake`。
CPU/Player 原始二进制保存于 `out/b4/baseline/bin`；所有后续比较必须使用各轮真实 exe hash，
不能把运行时工作区 revision 当编译版本。固定 CPU 输入及结果消费来自同一测量工具。

CPU 测量每 case 一次预热、五次重复；矩阵/Bounds 每次重复执行 16 批，提取每次 4 批，
环境每次重新生成一次。下表是五次平均，单位 ms；包含输出校验、分配/释放和 checksum 消费。
CPU/GPU 提取仅表示是否执行 CPU 视锥测试，不创建 GPU。

| case | 元素/实例 | 基线 ms |
| --- | ---: | ---: |
| matrix.multiply | 4096 | 0.080436 |
| matrix.inverse | 4096 | 0.372996 |
| bounds.transform | 4096 | 0.083049 |
| bounds.visible | 4096 | 0.081024 |
| scene.extract.cpu / gpu | 64 | 0.116380 / 0.080295 |
| scene.extract.cpu / gpu | 1024 | 2.344140 / 2.043305 |
| scene.extract.cpu / gpu | 4096 | 12.790450 / 12.308800 |
| environment.prefilter | 64×32、七 tile、128 samples | 476.522080 |
| environment.brdf | 64×64、128 samples | 14.856140 |

原始五次数据、warmup、checksums、源码与 exe hash 见 [baseline-cpu.json](b4-foundation/baseline-cpu.json)。
环境预滤波使用固定 32×16 非均匀图像；不代表完整 Renderer 构造时间，也不属于稳定帧成本。
目前只能说明完整提取远比单独矩阵乘法昂贵，不能从微基准推算固定 FPS 或断言每个内部步骤占比。

场景套件为 `sample/benchmark/foundation.json`，640×360/Medium、validation off、headless serial，
每项每后端三次，每次预热至少 8 帧/1 秒、测量至少 5 秒，重复时交替后端顺序。
四项 × 两后端 × 三次共 24 次、116445 测量帧；CPU profiling 仅独立诊断 case 开启。
下表是每次运行均值的等权平均，非窗口吞吐：

| case | DX12 CPU Render / serial frame ms | Vulkan CPU Render / serial frame ms |
| --- | ---: | ---: |
| PBR（探针关闭） | 0.7291 / 0.8787 | 0.6744 / 0.8648 |
| Shadow（探针关闭） | 0.7060 / 0.8576 | 0.6142 / 0.8004 |
| Ray Tracing（探针关闭） | 1.4362 / 2.2011 | 1.2579 / 2.0702 |
| PBR diagnostics | 0.7146 / 0.8644 | 0.6474 / 0.8301 |

诊断并非与关闭探针的结果配对隔离实验，不能据表推断“探针免费或更快”。
逐次均值、所有动态指标及内存快照见 [baseline-scenes.json](b4-foundation/baseline-scenes.json)，
完整 manifest/raw JSONL/分析与图像位于 `out/b4/baseline/scenes`。Runner 对源资产、Shader 和 exe hash
在前后核对并标记 complete；未删除长尾样本。源项目仍为只读输入。

## 验证

两配置全目标构建成功，CPU benchmark、Content、PBR CPU、Core、Graph、Benchmark 和双后端
PBR/Player 所选回归各 10/10；Python Benchmark 15/15。
三次 × 双后端的 PBR diagnostics 与关闭探针参考 BMP hash 全部一致，准备子区间均值之和
与 scenePrepareMs 一致。固定光追另按 8 帧预热/8 帧测量保存 16 spp 的后续参考，
见 `out/b4/baseline/fixed-ray`，双后端 validation errors=0。

已有 DX12 optimized-clear 不匹配及 Vulkan 未消费顶点属性警告仍存在，不把零验证错误表述为零警告。
其他 GPU、4K、长期压力和 M7 人工/DPI 验收不在本阶段；尚未发生数学库、SIMD 或 GPU 分配器迁移。
