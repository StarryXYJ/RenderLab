# M6 GPU Driven 验证与短测

日期：2026 年 10 月 29 日。接口见 [GPU Driven](../gpu-driven.md)，状态只登记于阶段计划。

## 配置与正确性

设备为 NVIDIA GeForce RTX 5060，DX12 驱动 32.0.15.9174、Vulkan NVIDIA 591.74；Windows x64 / MSVC 19.51，NMake Debug/Release。双配置全目标构建成功，所选 Content/Core/Graph/CLI、双后端 indexed_scene/descriptor_array/indirect/rhi/graph/render_state/PBR/Deferred/AS-Ray Query/反射各 24/24。GPU 验证错误 0，专项销毁后 RHI 账本为 0；Python Benchmark 数据校验 15/15。

GPU 列表与 CPU 八角/六面测试逐项一致：1/64/65/1024/4096 候选覆盖线程块边界，后三组分别得到 43/683/2709 个可见实例；稳定压缩保持每批次的实例顺序。另覆盖空场景、全剔除、PBR/Masked/透明、镜像、奇数尺寸、Ray Query 组合、在途版本和展开实例容量拒绝。容量负例直接调用索引投影，以隔离场景文档已有的 10000 对象限制。

同后端索引直接、CPU 间接、GPU 剔除的图像逐字节一致。CPU 相机剔除在 GPU 模式关闭，正常帧不映射 Count；可见列表读回仅用于显式诊断。其他 GPU、跨屏窗口和长时间压力未在本次验收。

## 测量方法

Release / validation off / headless serial，Deferred、257×193、固定 PBR 相机/曝光。规模场景复用同一静态网格与材质，固定 UUID 和网格位置，约三分之一放在视锥外；保留 PBR 样例原有的无光栅阴影设置。三种索引提交共享 Shader、材质表和批次；不能据此推断多材质、多阴影、高分辨率或任意场景的性能。

每项预热至少 4 帧且至少 0.25 秒，测量至少 1 秒，重复两次，后端顺序交替。Runner 保留真实 exe/Shader/项目 hash、命令、日志、全部 JSONL 和测量外截图，Analyzer 完整核对样本预算与汇总。每轮 9 case × 2 后端 × 2 重复 = 36 次。两个构建前后共 72 次、21665 个测量帧；单项样本量随帧耗时变化，本记录是短测，不构成统计显著性、稳定窗口 FPS 或后端优劣结论。

原始结果为本机 out/m62-benchmark 与 out/m62-benchmark-prepared；可移植汇总含两轮 exe/Shader hash、样本总数和每项均值，见 [summary.json](m6-gpu-driven/summary.json)。记录的工作区 revision 为 948eb93 且 trackedChanges=true；exe SHA-256 标识实际测试二进制，不把未提交工作区版本冒充该提交的构建。

## 结果

下表为最终 CPU 准备复用后的等权重复均值，单位 ms；帧时间包含串行 GPU 等待。

| 候选数 | 后端 | 索引直接帧 | CPU 间接帧 | GPU 剔除帧 | 单独剔除 Compute |
| --- | --- | ---: | ---: | ---: | ---: |
| 64 | DX12 | 0.919 | 1.080 | 0.898 | 0.005 |
| 64 | Vulkan | 0.795 | 0.862 | 0.819 | 0.007 |
| 1024 | DX12 | 4.554 | 4.976 | 4.329 | 0.047 |
| 1024 | Vulkan | 4.619 | 3.830 | 3.473 | 0.064 |
| 4096 | DX12 | 20.626 | 21.255 | 18.942 | 0.182 |
| 4096 | Vulkan | 19.798 | 21.683 | 18.149 | 0.244 |

GPU 剔除额外增加 GPU 工作，4096 实例的 GPU Pass 总和由索引直接的 DX12 0.715 / Vulkan 0.794 ms，变为 0.902 / 0.998 ms。该场景仍由 CPU 准备主导，不能把串行帧的变化全部归因于射线、GPU 着色或剔除 Compute 更快。

初测暴露了逐实例重复材质文件版本查询、材质/PSO 准备和对象线性查找。改为每帧冻结材质输入与策略、复用同材质/Pass/绕序的索引准备、按原 PSO 身份只创建一次索引 Pipeline，并使用临时对象 ID 索引。4096 实例的 GPU 提交路径由 DX12 145.399 / Vulkan 147.626 ms，降至 18.942 / 18.149 ms；这部分收益主要来自 CPU 准备复用，不是提高 GPU 采样或改变画面。两个构建之间 36 张截图逐字节相同；每轮各 12 组、每组 3 种提交的截图也全部相同。

4096 实例的 RHI tracked native 使用量：DX12 索引直接约 13.38 MiB、GPU 剔除约 13.62 MiB；Vulkan 约 13.10 / 13.19 MiB。包含固定上传页和场景资源，不能当作驱动实时显存或进程总内存。索引投影仍全量提取/上传，阴影与透明保留原 CPU 路径；一个线程组逐块处理一个批次也限制 Compute 并行度。默认保持 Direct，GPU 路径显式选择。

## 复现

按手册配置 Release 并启用 GPU 测试，先执行 gpu.dx12.indexed_scene 生成固定规模项目，再运行共享配置：

```powershell
ctest --preset windows-release -R 'gpu.dx12.indexed_scene' --output-on-failure
python tools/benchmark/run.py sample/benchmark/gpu-driven.json --output out/benchmarks/gpu-driven
```

共享配置使用标准 windows-release 输出路径；本机实际构建树为 out/build/m5-release，测量配置位于 out/m62-suite.json，只有 exe/生成项目路径不同。输出目录必须尚不存在。需要更长时间/更多重复时使用 --seconds、--warmup-seconds、--repeats；报告仍须保留测量条件。
