# RenderLab 阶段实施计划

本文件是实施状态的唯一记录入口。[architecture.md](architecture.md) 定义长期架构，[design-review.md](design-review.md) 解释本轮复查的取舍。阶段按可运行结果验收，只有设计或代码存在都不足以认定完成。

## 当前状态

- 当前实施阶段：B4 基础库与计算/分配优化；M6 核心 M6.1–M6.5、M0–M5、R1、B1–B3 DONE。可选 M6.0 Forward+ 仍为 DEFERRED。
- 当前工作项：2026 年 10 月 30 日用户继续授权 B4，已核对 B4.4b 在 `7011dd7` 中提交，B4.1–B4.4 DONE；当前 B4.5a 内存计费口径与诊断 IN_PROGRESS，按 a–c 独立验收和提交。M7 工作台 UI/UX 修订 DONE；M7.2f 完整持续输入/画质及混合 DPI 复验仍未关闭，本轮保留为 DEFERRED，不冒充完成。
- 用户约定：每个子阶段验收完成即精简计划、标注 DONE 并提交 Git，无阻塞时在当前授权范围内继续推进。本次继续时已有 B4.4a 未提交改动，已接续检查与验证；库候选按实测决定采用或延期，不以引入依赖本身作为性能成果。

| 阶段 | 目标 | 前置依赖 | 状态 |
| --- | --- | --- | --- |
| M0 | 工程与设备基础 | 无 | DONE |
| M1 | 最小双后端 RHI | M0 | DONE |
| M2 | Shader 与材质数据链路 | M1 | DONE |
| M3 | 单队列 Render Graph | M2 | DONE |
| M4 | 场景资源与基础 Renderer | M3 | DONE |
| R1 | Shader/Material 契约修订 | M4 | DONE |
| B1 | 样例整理与可扩展 Benchmark | M4、R1 | DONE |
| B2 | 双后端性能差异调查 | B1 | DONE |
| B3 | 逐帧资源复用优化 | B2 | DONE |
| B4 | 基础库、CPU 计算与 GPU 内存分配优化 | B3、M6 核心 | IN_PROGRESS（B4.1–B4.4 DONE；当前 B4.5a） |
| M5 | Deferred 与光追基础 | M4 | DONE |
| M6 | GPU Driven 与执行优化 | M4；光追调度测试另需 M5 | DONE（M6.1–M6.5；可选 M6.0 延期） |
| M7 | C# 编辑器与完整项目工作流 | M4；默认在 M5、M6 的核心目标后开展 | DEFERRED（已有实现/自动验收保留；M7.2f 人工复验及后续工作未关闭，本轮先执行 B4） |
| M8 | 一个深入的渲染专题与作品交付 | 所选专题依赖已满足 | NOT_STARTED |

默认顺序为 M0 到 M8。M5 和 M6 在 M4 后可以独立推进；单阶段内按下列子阶段顺序执行，不同时建设多个高级系统。完整编辑器后置，M4 只实现运行内容所需的最小项目与资源能力。

R1 契约修订、B1 样例整理/Benchmark、B2 调查和 B3 资源复用已完成，均按子阶段验证与提交。

## 状态与维护规则

状态使用 NOT_STARTED、IN_PROGRESS、DONE、BLOCKED、DEFERRED。通常只保留一个 IN_PROGRESS 主阶段，并在当前状态标出具体子阶段。BLOCKED 必须写明阻塞条件与恢复所需事项；DEFERRED 表示明确推迟，不能算作完成。

开始工作时更新当前阶段和工作项，按本次用户授权推进。任务复选框只表示子项完成，主阶段必须满足全部退出条件。代码未运行、测试未执行、硬件功能未验证时，直接写明，不能把“应该可用”记为通过。

每完成一个子阶段，在同一次文档更新中：

1. 核对该子阶段的交付和验收，记下实际使用的构建配置、后端和设备。
2. 把已完成子阶段的详细待办替换为简短完成记录，保留结果、验证入口及限制。未完成的后续部分继续保留详细计划。
3. 将长期接口说明放入 docs，构建运行步骤放入 manual；计划只链接入口，不复制整份说明或测试日志。
4. 更新主阶段状态和下一工作项。整个主阶段验收后，只保留一个简短完成摘要及主阶段标题；删除子阶段清单、提交列表和逐轮数据，细节通过 docs、manual 与 Git 历史追溯。

完成摘要格式如下，尖括号仅为模板占位，不能作为真实验证结果：

```text
### Mx.y 子阶段名称
状态：DONE，日期：<实际日期>
结果：<已实现能力和关键限制>
验证：<实际命令或复现入口、后端、设备和结论>
文档：<接口或使用说明路径>
后续：<仍需跟进的问题或无>
```

主阶段压缩后仍保留交付摘要、完成日期、验证入口和未解决问题。不要再建立一份复制全部待办的历史计划；过程可由版本历史追溯。代码修改使已完成能力失效时，重新标记受影响阶段或登记明确的修复工作项，不能保留过时的 DONE 结论。

## 通用验收约定

- 公共 RHI、Shader 与基础渲染能力需要在 DX12 和 Vulkan 验证；可选能力按能力矩阵记录实际支持与运行情况。
- 基础功能使用 Debug/Validation 检查，性能结果使用注明配置的构建；不混用两类数字。
- 保留小型、稳定的参考场景，固定相机、曝光和随机种子。失败应可缩小到一个资源或 Pass。
- 测试针对行为和边界，例如写后读、延迟回收、缓存失效和图剔除；不为简单包装或文档修改新增测试。
- 未支持的 GPU 能力可以明确列为硬件阻塞或推迟，不能用其他后端的成功替代该项验收。
- 阶段内发现接口不合理时，先修订最小设计再继续，不堆叠兼容分支维持尚未发布的旧接口。

## 已完成阶段

M0–M6 核心能力及 R1、B1–B3 已完成（2026 年 10 月 23–30 日）。下表保留结果、实际验证入口与限制；子阶段过程和逐轮数据通过 Git、链接文档追溯。

| 阶段 | 完成结果与验证摘要 | 接口、结果和复现 |
| --- | --- | --- |
| M0 | 工程、窗口、设备/能力报告；双后端 Debug/Release 9/9 | [基础](../docs/m0-foundation.md) |
| M1 | 双后端 RHI、同步、两帧上下文、资源/命令/计时；两配置 13/13 | [RHI](../docs/m1-rhi.md) |
| M2 | Shader/材质编译、反射、Schema、缓存与热重载；两配置 16/16 | [Shader/材质](../docs/m2-shader-material.md) |
| M3 | 声明式 Graph、依赖/剔除/屏障/池、Feature 组合；初验 19/19，后续回归见 M6 | [Graph](../docs/m3-rendergraph.md) |
| M4 | 固定场景/资源、PBR/IBL/阴影/Alpha、Player；两配置 37/37，后续阴影修复双后端专项通过 | [场景](../docs/m4-scene.md)、[阴影](../docs/m4-shadows-alpha.md)、[工具](../docs/m4-tools.md) |
| R1 | Shader/Material v2、参数 ABI、语义与迁移；两配置 40/40 | [契约](shader-material-v2.md)、[参数](../docs/shader-parameters.md) |
| B1–B2 | 固定场景 Benchmark、流式样本与双后端差异定位；两配置 41/41 | [Benchmark](../docs/benchmark.md)、[调查](../docs/performance/b2-pbr-rtx5060.md) |
| B3 | 上传/描述符/资源复用；两配置 41/41，360600 帧配对测量与参考图一致 | [复用](../docs/resource-reuse.md)、[实测](../docs/performance/b3-resource-reuse.md) |
| M5 | Deferred、AS/Ray Query/TraceRays、反射累积/空间与时间重建、误差控制及资源优化；双后端原生/GPU/窗口回归通过 | [Deferred](../docs/deferred-pipeline.md)、[光追](../docs/raytracing.md)、[时间重建](../docs/temporal-reflections.md)、[实测](../docs/performance/m5-adaptive-reflections.md) |
| M6.1–M6.5 | 索引/间接提交、GPU 剔除、并行录制、多队列与瞬态 Texture Alias；最终两配置各 28/28，补充回归各 5/5，双后端窗口错误/关闭账本为 0 | [GPU Driven](../docs/gpu-driven.md)、[Alias](../docs/memory-alias.md)、[实测](../docs/performance/m6-memory-alias.md) |

验证设备主要为 RTX 5060，早期 M3 为 RTX 4060 Laptop。GPU 回归的 validation errors=0 不代表无警告或其他设备通过；未验证项不得由上述数量推断通过。完整命令见 [构建手册](../manual/build.md)。

保留限制：静态场景/有限材质与纹理图集，单 mip/layer；无动态几何、完整设备丢失恢复、通用反射运动矢量或 4K/其他 GPU 完整验收。反射重建仍有模糊/亮度偏差/曲面拖影取舍，射线稳健偏移与 mip/footprint 延后。Alias 限同队列不重叠且首次完整 Clear 的兼容瞬态 Texture，不包括 Buffer/History；短测和串行延迟不能当窗口吞吐或普遍性能收益。

### M6.0 Forward+（可选组合）

状态：DEFERRED。不阻塞 M6 核心完成；需另行授权和建立灯光分块/剔除输入、正确性及性能对照，不能将现有 Forward 当作 Forward+。

## B4 基础库与计算/分配优化

状态：IN_PROGRESS，2026 年 10 月 30 日用户授权。依次实施下列子阶段；每阶段验证后将详细待办替换为结果、验证入口及限制，并单独提交。候选库需要固定版本、保留许可证、验证构建和数据契约；不因本计划自动建设完整资产管线、通用 Job System 或新渲染功能。

### B4.1 基线与可复现测量

状态：DONE，2026 年 10 月 30 日。

结果：独立 CPU kernel/64–4096 实例提取测量入口；可选 CPU 准备分解，关闭不读额外时钟；固定场景双后端基线与二进制/源码身份。
验证：MSVC Debug/Release 全目标构建成功，所选 CPU/Graph/Core/Benchmark/双后端 PBR/Player 两配置各 10/10；Python Benchmark 15/15。Release/RTX 5060 基线 24 次、116445 帧，六组探针 PBR 截图一致，准备子区间求和正确；双后端固定帧预算光追参考 validation errors=0（仍有既有警告；报告 16 spp，测量外截图 17 spp）。
文档：[指标与接口](../docs/benchmark.md#场景准备分解与-cpu-kernel)、[基线与限制](../docs/performance/b4-foundation.md)、[命令](../manual/build.md#cpu-与基础优化基线)。原始证据 `out/b4/baseline`。
限制：CPU 校验消费计入 kernel，环境 case 是重新生成而非稳定帧；固定小场景、短测和串行 headless 不代表窗口 FPS 或其他设备收益。

### B4.2 减少重复计算与几何算法

状态：DONE，2026 年 10 月 30 日。

结果：对象值比较复用已验证层级与 ID 索引；实际 world/localBounds 比较复用 normal/Bounds，失败不推进历史；TRS 仅做线性消元校验，Bounds 改为逐轴极值角以保持八角浮点结果。
验证：Debug/Release 全目标构建成功，各 18/18 CPU/CLI + 10/10 双后端 Alias/索引/反射/PBR/Scene 回归；5000 组 shear/镜像 Bounds 与原参考一致，拒绝边界和直接编辑失效合约通过。五次 CPU 配对的十二项 checksum 相同，20 组静态/固定帧图像 hash 相同；Release 场景配对 24 次、119630 帧，validation errors=0，既有警告保留。
文档：[缓存契约](../docs/m4-scene.md#cpu-提取缓存)、[测量与限制](../docs/performance/b4-foundation.md#b42-重复工作与-bounds)。证据 `out/b4/cache`。
限制：任意对象改变仍整体验证/解析层级；快照字符串/表和实例 world 投影仍按帧生成。视锥保留原八角基线，未因本阶段引入新的平面剔除语义；小 PBR 没有明确整帧收益，时长预算光追不能当相同 spp/Pass 工作的配对比较。

### B4.3 复用成熟数学实现

状态：DONE，2026 年 10 月 30 日。

结果：固定 DirectXMath 3.21（jun2026 / `93e6399`），math.cpp 内部复用乘法、转置、四元数组合；公共类型、对齐、列向量和 Pack 保持。Player 安装包含 MIT 许可证。一般求逆仍保留带原 pivot 诊断的实现，不把库的 Inf 返回行为当异常替代。
验证：两配置全目标构建成功，各 18/18 CPU/CLI + 10/10 双后端 GPU 回归；5000 组双精度乘法误差参考、四元数顺序、shear/边界/缓存合约通过。六组固定 PBR/Shadow/光追画面 hash 相同；Release 五次 4096 乘法均值 0.080497→0.043204 ms，未推断完整帧同比加速。
文档：[数学契约](../docs/m4-scene.md#场景与变换)、[测量与限制](../docs/performance/b4-foundation.md#b43-数学库)。证据 `out/b4/math`。
限制：只采用已验证的三类运算；向量、TRS、投影与一般逆保留项目契约，未验证 AVX/其他 CPU/平台，也未为一般求逆宣称新增收益。

### B4.4 批量 SIMD 与环境预计算

状态：DONE，2026 年 10 月 30 日。a 预计算与 b 内核/SIMD 对照已独立验收并提交；原提交权限阻塞已解除，后续限制如下。

结果：固定 sRGB/方向/切线基、BRDF 行样本与跨调用 LUT 复用；普通 C++ 局部分量计算保持原始逐元素结果。可选 ISPC 1.28.2 固定包/exe 校验，默认关闭，SSE2/AVX2 分派仅用于卷积，未滤波层/BRDF 保持精确 C++，无全局 fast-math 或 Job System。Highway 作为替代候选 DEFERRED。
验证：NMake/MSVC Debug/Release 全目标构建，普通 C++ 与可选 SIMD 各配置均完成 19/19 CPU/CLI + 10/10 双后端 GPU 集合；SSE2 单目标、空/尾部/能量/并发/所有权和编译器/目标拒绝通过，Python 15/15，许可证实际安装。最终 C++ 与 SIMD 固定场景图像 hash 与基线相同，validation errors=0（既有警告保留）。Release 暖预滤波由 a 的 310.1239 降至普通 C++ 44.2981 ms，ISPC 24.8959 ms；后者相对公平 C++ 基线约 1.78 倍，非稳定帧收益。
文档：[环境契约](../docs/m4-forward-pbr.md#环境光)、[预计算与 SIMD 实测](../docs/performance/b4-foundation.md#b44a-环境预计算)、[构建](../manual/build.md#可选环境-simd)；证据 `out/b4/{precompute,simd}`。
限制：SIMD 非逐位替代，常规/8192 轴向极端输入界为 2e-6/2e-4；未验证其他 CPU、真实无 AVX2 硬件、8192² 或长期压力。Ninja 沙箱 ABI 探针未完成并已取消，不冒充通过。两配置已恢复默认 C++；GPU 分配与资产库仍属后续未完成项。

### B4.5 GPU 内存分配

状态：IN_PROGRESS（a）。按 a–c 分别验收和提交，不同时改两个后端及 Graph Alias 算法。

- [ ] a：定义资源分配字节、底层保留块、驱动 usage/budget 与进程内存的区别，适配统一预算与诊断；现有原生 Alias 与普通分配统一计费，不漏计或重复计费。
- [ ] b：固定版本 D3D12MA 并接入 DX12 普通 Buffer/Texture/AS 用途；保留上传/描述符/资源池、完成点和 CPU/命令保活；验证预算、取消、延迟销毁、Resize 与关闭。
- [ ] c：固定版本 VMA 并接入 Vulkan 普通资源，处理映射、coherent 要求、device address/AS、分配对齐与销毁顺序；保留 Alias 原生路径的合法性与同步。
- [ ] 两后端 Debug/Validation、Release、Alias/多队列/光追及编辑器重建回归；对比加载、动态分配、稳定帧、底层块数/峰值/空闲回收。关闭后存活分配与最终保留块分别检查，不能将缓存容量隐藏为零。
- [ ] 首轮不实现 defragmentation、资源搬迁或自动驻留系统，不宣称减少 Shader 工作量或纹理本体大小。

### B4.6 资产处理库与整体验收

状态：NOT_STARTED。

- [ ] 评估 meshoptimizer 的导入后 cache/fetch 优化，保持顶点属性、三角形绕序、子资源身份和缓存版本；采用时覆盖光栅/光追及退化网格。
- [ ] 对照 cgltf 与当前解析、DirectXTex 与当前纹理契约，给出采用边界；完整 importer 迁移、mip/BC 的 RHI 扩展仅在具体需求和验证支持时实施，否则明确 DEFERRED。
- [ ] 汇总实际采用/未采用库、基线对照和限制；更新 docs/manual/总体设计，完成相关原生与托管回归。所有已采用路径验证完成才标记 B4 DONE；延期能力不算已实现。

## M7 C# 编辑器与完整项目工作流

本轮状态：DEFERRED，先执行用户新授权的 B4。以下实现证据保留；M7.2/M7.2f 中的 IN_PROGRESS 表示未关闭的既有验收，不代表本轮同时实施。

按 2026 年 10 月 30 日用户调整，先做给定固定场景下的 Dock 渲染工作台，暂不修改资源、材质或场景数据。后续再进入材质编辑与完整项目工作流。运行时规则继续由 C++ 服务维护。

### M7.1 桥接与视口试验

状态：M7.1a–g DONE（2026 年 10 月 27 日）。结果：Avalonia/MVVM/DI、代际 C ABI/UTF-8、子 HWND 与 MTA 渲染 owner、输入/关闭、帧诊断、按需刷新/Resize 合并及 0–240 FPS 调度。双后端自动自检、托管两配置和原生输入回归通过，关闭账本/验证错误为 0；运行证据 `out/m7/{mvvm-final,thread,scheduling,uncapped}`。接口与测量见 [宿主](../docs/editor-host.md)、[调度实测](../docs/performance/editor-scheduling.md)，构建见 [手册](../manual/build.md#avalonia-嵌入原型)。跨屏 96 DPI 和长期压力证据见 M7.2f；真实持续输入/人工画质及混合 DPI 仍需复验，已有自动 Present 不替代这些项目。

### M7.2 固定场景渲染工作台

状态：IN_PROGRESS；2026 年 10 月 30 日用户授权实现至 M7.2f。设计与契约见 [工作台](../docs/editor-workbench.md)，取舍与参考见 [设计复查](design-review.md#固定场景渲染工作台设计复查)。每个子阶段实施并验证后分别提交，硬件/人工未验证项明确保留。

#### M7.2a–e 已完成摘要

状态：DONE（2026 年 10 月 30 日）。各子阶段分别提交；接口见 [工作台](../docs/editor-workbench.md)、[宿主/ABI](../docs/editor-host.md)，操作见 [手册](../manual/build.md#avalonia-嵌入原型)。

| 子阶段 / 提交 | 结果 | 实际验证与证据 |
| --- | --- | --- |
| a–b / `779941e`、`e594733` | 默认 Scene/Settings、菜单诊断面板、布局/浮动、Serilog/原生日志；强类型运行配置、能力/版本确认、暂停/单帧、预设及跨宿主保留，源资产只读 | 托管两配置各 11/11；原生 Release 所选 11/11、Debug 4/4；双配置×双后端自检各 20 项、各 8 次正常及启动中关闭。`out/m72b/restored-{release,debug}-{dx12,vulkan}`、`tests` |
| c / `ced1c2e` | API/Validation 异步退休与首帧确认、代际隔离、配置/相机保留；一次回滚、Failed/Retry、取消过期事务 | 托管两配置各 14/14；Release/DX12、Debug/Vulkan 各 21 项和 10 轮 API 双向切换，暂停/最小化交叉，各 28 次关闭。`out/m72c/final-{Release-dx12,Debug-vulkan}`、`tests` |
| d / `1f307c1` | 实际提交 Plan 值快照；Pass×Resource、版本/依赖/队列/屏障/Alias/剔除、生命周期、搜索/缩放、Freeze/Live/导出 | 托管各 16/16、原生 Input/Graph 各 2/2；已知小图、容量/钉住/短缓冲/身份与非法引用通过；两起始后端各 22 项、各 29 次关闭。`out/m72d/{release-dx12,debug-vulkan,tests}` |
| e / `c639577` | 实际命令 Draw/Indirect/批次/实例/Dispatch/射线/AS/Copy；完成点确认的 CPU/GPU/内存、显式可见数读回、有界曲线/历史、同帧 Graph/导出 | 托管各 19/19、原生 Input/Graph/双后端 Indexed Scene 各 4/4；两起始后端各 23 项、各 30 次关闭；四档同尺寸观察短测每档 8 秒无丢样。`out/m72e/{accepted-Release-dx12,accepted-Debug-vulkan,tests,cost-*}` |

上述构建两配置均零警告；实机 RTX 5060/96 DPI，所有报告的关闭账本/validation errors/worker_failed 为 0，并核对 23 份源 Scene/Material/Project hash 不变。过程通过 Git 追溯；不同子阶段的用例数不相加作为覆盖数量。

保留限制：目标/回滚失败以可控状态机测试覆盖，未注入真实设备丢失；图快照单份 4 MiB、矩阵单视图 128×256，不复制 RT 像素。GPU 超过 32 Pass 计时预算仍可渲染；默认间接可见数未知。短测不代表固定性能收益或显示延迟，成本解释见 [测量](../docs/performance/editor-workbench.md)。人工/跨屏/长期及日志故障证据见 f。

#### M7.2f 固定场景集成验收

状态：IN_PROGRESS（2026 年 10 月 30 日实现及自动验收完成；下列人工/硬件复验未关闭，不提前将 M7.2 标为 DONE）。

结果：新增隔离布局/预设和墙钟长测模式，按预算流式记录、轮换三管线/诊断/布局，移动实际屏幕并校验原生像素。v10 原生日志文件写失败保留镜像、托管文件错误独立显示，修复取消 API 事务后 Dock 新宿主的实际后端标识同步。

验证：最终 Debug/Release 日常构建均零警告、托管各 22/22；原生 Core 两配置通过。Release/DX12、Debug/Vulkan 最终自检各 23 项，含配置/API、Dock 浮动/恢复、同帧 Graph/Profiler、末尾 drain；各 30 次正常 Session 及初始化中关闭账本/验证错误/worker_failed 为 0。文件实际写失败、滚动/消息洪峰有独立用例。Release 双后端实际运行 1800.87/1800.98 秒，观察到 53776/53779 帧，无丢样；各 4 次关闭全为 0，23 份源 hash 不变，所有历史/内存预算检查通过。两块实屏均 100%/96 DPI，根窗口跨屏后的像素尺寸匹配。证据 `out/m72f/{final-release-dx12,final-debug-vulkan,soak-dx12,soak-vulkan,tests}`；方法、内存峰值和版本边界见 [实测](../docs/performance/editor-workbench.md)，命令见 [手册](../manual/build.md#avalonia-嵌入原型)。

待验收：可见桌面已观察默认固定场景、Graph/Profiler，系统点击菜单/诊断页可用；持续按键、右键拖动/捕获释放、完整浮动窗口的系统输入与用户画质认可，以及 150%/混合 DPI 仍未验证。合成输入、自动 Present 与两个 96 DPI 屏幕不替代这些项。真实设备丢失、其他 GPU、资源/场景编辑不属于本轮验收覆盖。

#### M7.2 工作台 UI/UX 修订

状态：DONE（2026 年 10 月 30 日，用户追加授权并确认按当前版本收尾）。结果：中性灰紧凑主题、双列属性 Inspector、精确曝光输入、顶部图标工具栏、固定预设操作、Profiler 指标/曲线/可滚动 Pass 表、Graph 工具栏/访问图例/可调详情栏及 Live 选择保留、分列/分级着色日志与完整事件详情。默认仍只有 Scene/Settings；已有保存布局继续恢复。

验证：最终 Debug/Release 构建零警告、托管各 22/22。Release/DX12 窗口自检与末轮排版后的 Debug/Vulkan 自检各 23 项、各 30 次正常 Session 及启动中关闭账本/验证错误/worker_failed 为 0；修订焦点自检以实际展开后的控件接收焦点，暂停命令检查图标与 tooltip。可见窗口检查了默认场景、分组/滑条/关闭按钮，以及 Graph 与 Profiler 的真实渲染；用户停止 computer-use 后未继续界面操作。证据 `out/ui-workbench/{verified-release-dx12,final-debug-vulkan,tests}`。本轮未重跑 30 分钟长测或混合 DPI，M7.2f 的既有限制继续保留。设计见 [工作台](../docs/editor-workbench.md#工作台视觉与布局)，操作见 [手册](../manual/build.md#avalonia-嵌入原型)。

### M7.3 材质编辑与事务

状态：DEFERRED（本轮范围外，原 M7.2）。

- [ ] Schema 自动 Inspector、未保存材质预览、参数验证。
- [ ] Begin/Update/Commit/Cancel 编辑事务，连续拖动形成一条 Undo。
- [ ] UI 快照和命令队列不直接暴露 GPU 对象，编译事件显示错误位置。

### M7.4 项目与资源操作

状态：DEFERRED（本轮范围外，原 M7.3）。

- [ ] 创建/打开/关闭项目、资源树、搜索、导入进度。
- [ ] Move/Rename/Copy/Delete 调用统一资源服务，处理引用与冲突。
- [ ] Library 重建、损坏缓存恢复、编辑资源与外部变更冲突提示。

### M7.5 场景编辑

状态：DEFERRED（本轮范围外，原 M7.4）。

- [ ] Hierarchy、选择、Transform、拾取、Gizmo、组件属性与保存。
- [ ] Undo、dirty 状态、关闭未保存文档和重开场景。
- [ ] GPU Scene 随编辑正确更新，历史效果在必要时失效。

### M7.6 运行与构建

状态：DEFERRED（本轮范围外，原 M7.5）。

- [ ] 独立 Player 和内容处理，按配置收集资源与必需 Shader 变体。
- [ ] 从导出目录独立运行，不依赖原项目 Library 或开发机器路径。
- [ ] 原生崩溃保留诊断；首版不开发进程隔离和 IPC 恢复系统。

M7.2 出口：给定固定场景下，两面板默认布局与菜单按需打开的 Graph/Profiler/Log、Serilog 日志、运行配置、双后端切换完成上述集成验收。M7 整体出口仍为创建项目、导入模型、从 Shader 创建材质、编辑场景、保存重开、导出运行全流程通过；本轮不提前标记 M7 DONE。

## M8 专题深化与作品交付

先选择一个主专题并写清目标、参考基线和必要输入，其他专题保持 DEFERRED，不默认全部实施。

候选为 TAA 与运动矢量、进阶光追反射与降噪、基础路径追踪、Hi-Z/LOD GPU Driven。Path Tracing 使用独立组合：场景/AS → Trace/采样 → History 累积 → 可选降噪 → ToneMapping；替代普通光栅主体，共用资源图和基础材质，不插在 Transparent 阶段。路径追踪先验证采样、BSDF/PDF、累积和收敛，再据测量决定是否建设 Wavefront 队列；降噪前保留原始结果作为正确性入口。

- [ ] 明确选择、支持范围与对照条件，拆成可验收的小步。
- [ ] 建立正确性参考，固定随机序列与相机并处理历史失效。
- [ ] 提供不同场景/规模的 CPU、GPU、内存数据与 Capture。
- [ ] 记录局限、误差和退化情况，避免只展示一个有利场景。
- [ ] 发布可运行演示、构建说明、操作说明、录屏和技术文章。

退出条件：所选专题完成且可复现，作品能由他人运行，讲解能从画面追踪到程序、资源和 GPU 执行。
