# M6.5 瞬态内存 Alias 验证与测量

日期：2026 年 10 月 29 日。规划范围、原生激活、整组池退休与提交事务见 [内存 Alias](../memory-alias.md)。默认关闭，开关只影响同 Graphics Queue 上首次附件 Clear 的兼容瞬态纹理。

## 环境与复现

Windows x64、MSVC 19.51 / CMake NMake、NVIDIA GeForce RTX 5060；Debug/Release 构建树为 `out/build/m5-nmake` 与 `out/build/m5-release`。性能使用 Release、validation off、CPU profiling off、GPU profiling on、Deferred/GpuCulled、固定相机。每次至少预热 4 帧与 0.25 秒，测量至少 2 秒，重复两次。

[memory-alias.json](../../sample/benchmark/memory-alias.json) 比较 PBR64（257×193）、原有 Shadow（257×193、1280×720，场景原定 512 阴影分辨率）。PBR 场景虽禁用阴影，现有 Renderer 仍生成 1×1 占位阴影深度，因此它也有小规模复用机会，不能当作无 Alias 候选的负对照。分别在 1 线程/单队列与 4 线程/异步 Compute 下开关 Alias；不改变光照、材质、相机或物体布局来制造收益。功能回归另覆盖全部线程/队列/Alias 开关组合。

```powershell
ctest --test-dir out/build/m5-release -R 'rendergraph.contracts|\.alias$|indexed_scene' --output-on-failure
python tools/benchmark/run.py sample/benchmark/memory-alias.json --exe out/build/m5-release/renderlab_player.exe --output out/benchmarks/memory-alias
```

先由 indexed_scene 测试生成 PBR64。示例默认引用 `windows-release`；本机实际配置为 `out/m65-suite.json`。原始运行在 `out/m65-benchmark`；输入、二进制、Shader、逐次指标、样本/报告/图计划/截图 hash 见 [summary.json](m6-memory-alias/summary.json)。sourceRevision 是父提交，trackedChanges=true，实际测试二进制以 SHA-256 标识。

## 正确性与限制

Debug/Release 全目标构建成功，所选 CPU/CLI 与双后端专项各 28/28。准备失败回收专项在 Release 追加通过；最终公共头文件依赖补全后重新构建，各追加定向 5/5，结果保存在 `out/m65-*-audited-*`。Python Benchmark 校验 15/15。原生 GPU 测试要求 validation errors=0，场景/池销毁后原生账本为 0。

CPU 合约检查同 Pass 重叠、延长读取、导入/导出、格式不兼容与非 Clear 首次访问的排除，以及 16 成员上限、深度/Stencil 初始化、实际状态/活动成员的成功发布、取消不发布、外部 View 保活、按组容量淘汰。额外覆盖准备阶段租出组但尚未建立纹理/视图即失败，下一图仍可复用；首次入队前/后失败与真实 drain 的保活继续受 Runtime 事务保护。

双后端原生测试使用不同尺寸的颜色附件，逐像素验证 Clear/Readback；比较关闭/开启 Alias 和 1/4 线程，两个在途图占有不同组，完成后连续三次复用，取消后再次复用，Trim 后字节/资源数归零。D32、D32S8 和带 Storage 用途的颜色附件另连续三帧激活，检查原生布局/元数据初始化。共享 backing 只计费一次，任一成员保活直到最后一份引用释放。

场景测试覆盖全部 1/4 线程 × 单/双队列 × Alias 开关、Forward/Deferred/混合光追、阴影、透明 fallback、Masked、镜像、奇数尺寸、空/全剔除与在途相机版本，像素与基线一致。渐进 History 的导入/导出资源不参与 Alias，开启选项的两个在途帧最终 3 spp 与独立串行参考一致；该光追配置不保证存在可复用组，不将选项开启等同于实际 Alias。

Release 窗口 validation on、GpuCulled、4 线程、异步、Alias、640×360：两后端各 54 帧、10 次重建、10 次挂起，错误 0，证据在 `out/m65-window`。既有 optimized-clear/未消费顶点属性警告保留，不算作验证错误。未完成人工画面检查、设备丢失、长期压力、多视口或其他 GPU 验收。

首版只共享相同格式/attachment/storage 类别的瞬态附件，尺寸可不同，最多 16 成员；不支持 Buffer、跨队列生命周期或外部 Alias 导入，也不尝试联合重排调度以增加节省量。内存数据是 RHI 跟踪的原生资源分配，不是驱动总驻留显存。普通池估算字节不能替代此账本。

## 原生分配与 Capture

不同尺寸 RGBA8 原生用例的独立分配 → 共享分配：DX12 为 393216 → 262144 字节，Vulkan 为 352256 → 278528 字节。两配置一致；共享块保活至最后一个成员释放，退出账本为 0。它们的物理对齐/平铺开销不同，不按逻辑 texel 字节推断原生用量。

RenderDoc 1.46 便携包与工具 hash 沿用 M6.4。临时 wrapper 链接生产 Release 库，捕获原有 Shadow 场景、4 线程、异步和 Alias，内部像素逐字节等于普通单队列参考。原始 `.rdc`/XML 在 `out/m65-capture/{dx12,vulkan}/Shadow`；工具、wrapper、捕获与图计划 hash，以及原生分配/屏障/首用 Clear 见 [capture-summary.json](m6-memory-alias/capture-summary.json)。没有执行图形 Replay，捕获耗时不计入性能数字。

- DX12：Heap 413 为 1048576 字节的 RT/DS Heap；512×512 的 resource 414 与 257×193 的 resource 415 都位于 offset 0。两次 aliasing barrier 为 null → 414、414 → 415，两个深度 Clear 均 NumRects=0（完整 View）。placed resources 使用与普通路径一致的 optimized clear 值。
- Vulkan：ALIAS image 426/427 绑定同一 memory 428、offset 0，共 1048576 字节；实际 requirements 分别为 1048576/278528 字节，对齐 1024、memoryTypeBits=3。两次全局内存依赖后，各有 UNDEFINED → DEPTH_STENCIL_ATTACHMENT_OPTIMAL 转换，两个附件首次使用均 Clear。

图计划显示 MainLightShadowDepth 生命周期 [2,2]，CameraDepth [3,5]，都在 Graphics Queue。Capture 的 chunkIndex 是 CPU 录制事件，不代表 GPU 执行顺序；实际执行沿同 Graphics Queue 的提交顺序，独立 Compute feeder 不访问这些纹理。缓存循环/取消/在途组隔离由前述原生测试覆盖。

Vulkan 首次抓帧因未加载便携捕获层而未启动，没有生成有效 Capture。成功重试只在该子进程设置 `VK_LAYER_PATH` 指向便携包、`VK_INSTANCE_LAYERS=VK_LAYER_RENDERDOC_Capture`、`ENABLE_VULKAN_RENDERDOC_CAPTURE=1`，没有系统安装或注册表更改。性能套件在新的独立进程运行，确认未启用捕获层。

## 内存、时间与默认策略

最终 48 次、74974 个测量帧全部通过完整性检查；六组后端/场景各八张截图逐字节一致。全部实际复用组都是 MainLightShadowDepth → CameraDepth，首尾生命周期、Graphics 访问和激活点另由汇总脚本核对。以下均为两次运行均值的平均，箭头为关闭 → 开启 Alias。

两种执行模式的 tracked 原生分配数字相同，单位 MiB（1 MiB=1048576 字节）。used 为报告时仍持有的资源与缓存，peak 包含预热/启动阶段；不把进程 RAM、描述符计数或普通池逻辑估算混入此表。

| 后端 | 场景 | used MiB | peak MiB | 减少字节 |
| --- | --- | --- | --- | --- |
| DX12 | PBR64 | 13.5625 → 13.5000 | 13.6250 → 13.5625 | 65536 |
| Vulkan | PBR64 | 13.1013 → 13.1008 | 13.1015 → 13.1010 | 512 |
| DX12 | Shadow257 | 18.8125 → 18.5625 | 18.8750 → 18.6250 | 262144 |
| Vulkan | Shadow257 | 18.1025 → 17.8369 | 18.1028 → 17.8372 | 278528 |
| DX12 | Shadow720 | 82.0625 → 81.0625 | 82.1250 → 81.1250 | 1048576 |
| Vulkan | Shadow720 | 81.0088 → 80.0088 | 81.0090 → 80.0090 | 1048576 |

时间单位 ms。1/G 表示 1 线程/单队列；4/G+C 表示 4 线程/异步 Compute。GPU span 是已计时 Pass 的范围，包含中间等待，但不包含首尾未计时工作，不能替代整帧时间。

| 后端 | 场景 | 执行模式 | 帧时间 | GPU span |
| --- | --- | --- | --- | --- |
| DX12 | PBR64 | 1/G | 1.0481 → 1.0186 | .0427 → .0378 |
| DX12 | PBR64 | 4/G+C | 1.1701 → 1.1497 | .1087 → .1033 |
| Vulkan | PBR64 | 1/G | .9202 → .9675 | .0462 → .0491 |
| Vulkan | PBR64 | 4/G+C | 1.1710 → 1.1928 | .2455 → .2452 |
| DX12 | Shadow257 | 1/G | 1.3125 → 1.3029 | .0537 → .0514 |
| DX12 | Shadow257 | 4/G+C | 1.4123 → 1.4154 | .1165 → .1138 |
| Vulkan | Shadow257 | 1/G | 1.1366 → 1.1360 | .0561 → .0603 |
| Vulkan | Shadow257 | 4/G+C | 1.3972 → 1.4202 | .2499 → .2597 |
| DX12 | Shadow720 | 1/G | 1.3991 → 1.4724 | .1287 → .1243 |
| DX12 | Shadow720 | 4/G+C | 1.5778 → 1.5512 | .1915 → .1858 |
| Vulkan | Shadow720 | 1/G | 1.2806 → 1.2482 | .1292 → .1335 |
| Vulkan | Shadow720 | 4/G+C | 1.4880 → 1.5752 | .3281 → .3394 |

内存节省来自少分配一块深度纹理 backing，和原生对齐相关；不是消除同一资源的跨帧版本，也不是减少场景渲染工作。关闭阴影时的占位资源节省很小，不能把其逻辑字节与原生分配等同。没有为增加节省量额外串行化 Pass。

帧时间有升有降，GPU span 的变化也不构成普遍速度收益。只有两次短时运行，模式顺序固定，探针与系统调度会影响结果，不据此宣称统计显著收益或窗口 FPS 提升。默认保持 `memoryAlias=false`，由使用者在需要减少瞬态 backing 时独立开启；CPU 录制与异步默认策略不变。
