# B4 基础库与计算/分配测量

日期：2026 年 10 月 30 日。实施状态只登记于 [阶段计划](../../design/phaseplan.md)。
指标和计时入口见 [Benchmark](../benchmark.md#场景准备分解与-cpu-kernel)，运行见 [手册](../../manual/build.md#cpu-与基础优化基线)。

## 原始基线

本机 Windows x64、Intel Core Ultra 7 265K、MSVC 19.51.36257，Release。GPU 为 RTX 5060，DX12 驱动 32.0.15.9174，
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
与 scenePrepareMs 一致。固定光追另按 8 帧预热/8 帧测量保存后续参考，报告为 16 spp，
测量外的额外截图为 17 spp，见 `out/b4/baseline/fixed-ray`，双后端 validation errors=0。

已有 DX12 optimized-clear 不匹配及 Vulkan 未消费顶点属性警告仍存在，不把零验证错误表述为零警告。
其他 GPU、4K、长期压力和 M7 人工/DPI 验收不在本阶段；该原始基线未包含数学库、SIMD 或 GPU 分配器迁移。

## B4.2 重复工作与 Bounds

沿用相同设备/配置/测量程序及五次 CPU 重复，源码与实际 exe hash 见
[cache-cpu.json](b4-foundation/cache-cpu.json)。所有十二项输出 checksum 与基线完全相同。

| case | 原始 / 缓存后 ms |
| --- | ---: |
| Bounds transform，4096 | 0.083049 / 0.022840 |
| CPU 提取，64 | 0.116380 / 0.038620 |
| CPU 提取，1024 | 2.344140 / 1.120230 |
| CPU 提取，4096 | 12.790450 / 5.898580 |
| 跳过 CPU 剔除的提取，4096 | 12.308800 / 5.100400 |

静态文档避免重复对象验证/层级解析，未变实例避免逆转置和 Bounds 计算；完整快照、字符串/表分配、
实例 world 投影与可见性仍按帧生成。它不是一般局部层级增量系统，移动相机仍会重建层级，
但静态实例的 normal/Bounds 可继续复用。冷帧和变化帧仍需计算，不把暖缓存结果推广为所有帧。
没有改变的逆矩阵控制项也从 0.373 波动到 0.331 ms，提示短测/系统频率的影响，不能将全部数字差异归因于改造。

场景套件再执行 24 次、119630 帧，完整 manifest/raw/分析位于 `out/b4/cache/scenes`。
关闭探针的 PBR serial frame：DX12 0.8787→0.8764 ms，Vulkan 0.8648→0.8487 ms，
没有足够证据宣称小场景整帧显著改善。Shadow 为 0.8576→0.7623 / 0.8004→0.7603 ms，
仍只是一轮三次短测。PBR diagnostics 与正式结果分开。

时长预算 Ray Tracing 的实际 spp 不同，且可能在 2048 spp 后旁路重建 Pass；两轮因此并非相同 GPU
工作序列，不能将该项 CPU/GPU 均值差异作为独立缓存加速结论。固定帧预算只用于本次正确性对照。
静态 PBR/diagnostics/Shadow 的 18 对图像及双后端固定 17 spp 的两对图像均逐字节一致。

Debug/Release 全目标构建成功，各 18 项 CPU/CLI 和 10 项双后端 Alias、Indexed Scene、
Reflections、PBR、Scene 回归通过。Math 独立覆盖 5000 组非均匀缩放/镜像/shear 的八角对照及
原 TRS pivot 拒绝边界；Scene 覆盖父级/相机/材质/模型节点/Bounds、启用/删除/重插/重排与失败事务。
所有报告 validation errors=0，既有警告保留。尚未以新视锥平面测试替换八角基线。

## B4.3 数学库

固定 DirectXMath 3.21 / jun2026，Git `93e6399d6d15e1e57f80b5cea04d5a8eb892d693`。
仅 math.cpp 使用库的矩阵乘法、转置与按参数顺序适配的四元数组合，不泄漏库类型或改变 GPU 布局。
默认 Windows x64 SSE2 路径，没有全局开启 AVX、FMA 或 fast-math。
一般求逆保留现有有限性及 pivot 诊断；未以“返回 Inf 的库调用 + 单个 determinant 阈值”
替代尺度相关的原拒绝契约，也未宣称该项有新性能收益。

相同五次 CPU case 的数据见 [math-cpu.json](b4-foundation/math-cpu.json)。
4096 次矩阵乘法由 B4.2 的 0.080497 降至 0.043204 ms；其 checksum 有预期的浮点结合顺序差异，
通过逐元素双精度参考界验证，而非要求和原求和 bitwise 一样。逆矩阵控制项为 0.330558→0.329750 ms。
4096 CPU 提取为 5.898580→5.946085 ms，不能据微基准推断整帧或完整提取同比加速。

Debug/Release 全目标构建成功，各 18 项 CPU/CLI 与 10 项双后端 GPU 回归通过；
5000 组随机矩阵乘法（双精度参考）、四元数组合顺序、转置、shear 与缓存边界通过。
双后端固定 PBR/Shadow 与 17 spp 光追六对图像 hash 与 B4.2 相同，validation errors=0，
既有警告保留。`out/b4/math/install` 实际安装了 Player 与 DirectXMath MIT 许可证，
尚未将该安装检查描述为其他机器独立运行验收。未验证其他 CPU 或 ISA。

## B4.4a 环境预计算

沿用同机 Release、五次 CPU kernel 入口，数据见
[precompute-cpu.json](b4-foundation/precompute-cpu.json)。改造前对照为 B4.3 的 math-cpu，
均包含输出消费与分配/释放；cold 为本进程首次调用，不含 exe 启动，BRDF 的 cold 和环境表
初始化相互独立。这是单轮短测，采样时另一个 Debug 构建仍在收尾，不能将全部时差归因于优化。

| case | 改造前 cold / warm 均值 ms | 预计算 cold / warm 均值 ms |
| --- | ---: | ---: |
| environment.prefilter，32×16 输入 | 458.8981 / 458.4311 | 307.3955 / 310.1239 |
| environment.brdf，64×64 输出 | 14.1717 / 14.1332 | 12.0811 / 0.00924 |

sRGB 256 项、64×32 输出切线基和每层 128 个方向共享只读表；BRDF 半向量按行复用，
完整固定 LUT 跨调用复用、返回独立副本。没有引入 SIMD 编译器或并行任务；常量表约 83 KiB、
BRDF 像素 64 KiB 在进程内保留，不算 GPU 资源，也没有保存每个输入图像的缓存。
冷/暖预滤波约减少 32%，不能将该生成成本当稳定帧 FPS 收益；BRDF 暖成本主要变为拷贝和校验。

Debug/Release 全目标构建成功，各 19/19 CPU/CLI + 10/10 双后端 Alias/索引/反射/PBR/Scene 回归；
环境参考逐元素相同（包括 BRDF、1×1、
3×5、65×33、2×1 非均匀图和经纬接缝脉冲），恒定白色保持能量，并发首次初始化和编辑副本隔离通过。
测试参考最初有一个位反转掩码抄写错误，修正为原版掩码后才作一致性结论；没有放宽误差阈值。
双后端 PBR/Shadow 的 640×360 及光追的 257×193、固定 17 spp 六组图像 hash 与 B4.3 相同，
validation errors=0，既有警告保留。原始证据 `out/b4/precompute`；误用 640×360 的首次光追
捕获不参加 hash 配对，正确尺寸的证据为 `fixed-ray-257`。其他 CPU/ISA 和长期运行未验证。

## B4.4b C++ 内核与可选 SIMD

固定 ISPC 1.28.2，官方包与 exe 哈希见 [构建手册](../../manual/build.md#可选环境-simd)。
默认关闭；开启时只编译环境内核，输出像素对应 lane，按图块组织 uniform 工作，样本按原次序累加。
使用 system math、关闭 FMA，无 fast-math、task 或全局 AVX。SSE2 单目标和 SSE2/AVX2
运行时分派分别实际运行，后者在本机支持 AVX2；未用真实无 AVX2 的机器验证自动选择。
Highway 未引入，保留为 DEFERRED 替代候选，不是缺失的第二套必选工具链。

首轮原 C++ 约 309 ms、SSE2 约 31 ms、分派约 24 ms 的差距还包含通用 Vec3 非内联调用、
整数模和循环组织，不能全归因于 SIMD。最终普通 C++ 也改为局部分量运算和有界经度环绕，
保持浮点结合顺序、与冻结原实现逐元素相同。对照采用这条 MSVC `/O2` 的正常优化路径，
没有全局改写数学库，也没有关闭自动向量化来制造差距。

同机 Release，固定 32×16 非均匀源、一次 cold 加五次 warm，计时继续包含分配和输出校验。
最终两次测量顺序为 C++、ISPC；测量前构建/回归已结束（ISPC 自身构建和参考检查在其测量前完成）。
原始数据含所有样本和真实 exe hash：
[kernel-cpp-cpu.json](b4-foundation/kernel-cpp-cpu.json)、
[kernel-ispc-cpu.json](b4-foundation/kernel-ispc-cpu.json)。

| 实现 | 预滤波 cold / warm 均值 ms | BRDF cold / warm 均值 ms |
| --- | ---: | ---: |
| B4.4a C++ 预计算（上一轮） | 307.3955 / 310.1239 | 12.0811 / 0.00924 |
| 最终局部整理 C++ | 42.7309 / 44.2981 | 12.4983 / 0.00732 |
| 最终可选 ISPC 分派 | 24.7899 / 24.8959 | 12.3381 / 0.00732 |

可选 ISPC 相对最终 C++ 约 1.78 倍，而不是宣称 12 倍纯向量化收益；BRDF 没新增 SIMD 加速。
新 C++ 的预滤波 checksum 与原版完全相同，ISPC 有预期的舍入差异。短测顺序、系统调度和
其他 case 的波动仍是限制；不以这些数字推算窗口 FPS、完整 Renderer 加载或其他 CPU 收益。

常规非均匀/接缝脉冲的最大分量差为 `2.68220901e-7` / `2.42143869e-8`，
8192×1 为 `8.94069672e-8`，高频 1×8192 为 `1.03712082e-4`。相应 fixture
分别用 `2e-6` / 极端尺寸 `2e-4` 绝对误差界，非任意输入的数学证明。tile 0 留在精确 C++
路径，BRDF 同样严格一致；确定性、恒定能量、非法输入、空区间、非零起点、3/4/5/7/8/9/65
及 14333 像素尾部的写入 guard 均通过。早期将未滤波层也交给 ISPC 时在 1×8192 放大到
`3.002882e-4`，因此未采用该路径，而非仅放宽全部输出的阈值。

NMake/MSVC Debug、Release 全目标构建成功，最终普通 C++ 各 19/19 CPU/CLI + 10/10
双后端 GPU 回归；可选 SIMD 两配置也完成相同集合，之后的 C++ 局部整理在最终 Release
SIMD 参考检查中再次覆盖。GPU 为 RTX 5060，普通 C++ 和 SIMD 各六组固定场景图像与原基线 hash 相同，
validation errors=0，既有警告保留。Python Benchmark 15/15（沙箱临时目录权限失败后，获批重跑通过）。
实际安装包含 ISPC BSD 许可证和第三方声明。SSE2、分派、配置拒绝和安装证据位于 `out/b4/simd`。

保留限制：默认仍 C++；SIMD 不是无损替代，没有验证其他 CPU/ISA、完整 8192² 图像或长期压力。
Ninja 新目录补测停在沙箱的编译器 ABI 探针，已取消，未记为通过；本轮可选路径的实际构建证据为
NMake，不以已有 Ninja 预设推断该补测通过。核外没有新的库、资源导入、GPU 分配或 Job System。
