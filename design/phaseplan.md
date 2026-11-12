# RenderLab 阶段实施计划

本文件是实施状态的唯一记录入口。[architecture.md](architecture.md) 定义长期架构，[design-review.md](design-review.md) 解释本轮复查的取舍。阶段按可运行结果验收，只有设计或代码存在都不足以认定完成。

## 当前状态

- 多光源多物体压测场景 IN_PROGRESS（2026 年 11 月 12 日）：在现有两盏矩形灯与一盏主方向光上限内创建可运行项目，验证不同管线、数量和性能；保留并行 M8 变更。

- 面光源非路径追踪阴影平滑修复 DONE（2026 年 11 月 12 日）：光栅阴影的深度比较及遮挡物搜索改为双线性加权，并按接收点灯面投影限制搜索，减少阶梯与空房间错误暗斑；混合管线改为固定 32 点抖动分层采样和深度/法线引导的 3×3 空间滤波。隔离 Debug/Release 的 DX12/Vulkan 光栅、HDR、反射、滤波与材质测试各 10/10；空房间内部显示值对 Ray Query MSE 为 0、旧搜索负对照为 24.92，遮挡边界子纹素最大跳变由 0.25 降至 0.0254；静止帧原始可见性一致。RTX 5060、512²、单灯短测与性能代价见 [面光源](../docs/area-lights.md#有限性能测量)；Windows Release Editor bridge 和托管构建通过。有限范围的单张中心阴影图仍可能漏掉近灯小遮挡物，表面边界仍是近似；混合阴影至多两灯共 64 射线且未做时域滤波，长时运动、复杂场景及更多 GPU 未验收。

- HDRP 参考面光源功能基线 DONE（2026 年 11 月 10 日）：Forward、Deferred、Indexed 和透明接收使用共享矩形灯、LTC、独立阴影图及 PCSS 式近似；可见灯面参与深度；混合光追使用逐灯 Ray Query 遮挡、解析灯面反射及反射命中物面积采样。最多两盏单面恒定辐亮度灯，支持剪切变换；LTC 数据来源与许可随产品分发。隔离 Debug/Release 的 RTX 5060 DX12/Vulkan 数值、场景、材质、反射、阴影及 HDR 对照通过；Release 窗口回归双后端通过，Editor bridge 与托管 Release 构建通过；无灯路径追踪双后端 128 spp 图像与接入前 hash 一致。Intel 核显 DX12 Forward/混合各 8 帧短测无验证错误；Intel Vulkan 在 Runtime 创建前受下述既有限制阻塞，未验证此设备面灯。Cornell 单/双灯 HDR 对照和 RTX 5060 单机短测、验证入口、完整限制见 [面光源](../docs/area-lights.md) 和 [手册](../manual/build.md#面光源短测)。这是所授权功能的近似基线，并非完整 HDRP：中心图有视场、近平面与漏光限制；有限采样有噪声和偏差；阴影缓存、EVSM、质量配置、更多设备/场景、长期运动及性能尚未验收，ReSTIR 与面灯烘焙未实现。M8 并行工作仍按其状态推进。

- M1 Intel Vulkan 时间戳兼容性 TODO（2026 年 11 月 10 日发现）：Intel 核显 Vulkan 队列 `timestampValidBits` 非 64，现有 `VulkanRuntime` 在创建时拒绝设备；Forward/混合面光源均未进入渲染。需设计有限位时间戳回绕与计时契约并验证，不把 DX12 或 RTX Vulkan 的通过等同于此设备通过；日志 `out/area-light-benchmark/intel-vulkan-{forward,raytracing}.log`。

- 矩形面光源子项 DONE（2026 年 11 月 10 日）：Path Tracing 支持最多两盏单面解析矩形/剪切平行四边形灯、有限遮挡、均匀选灯及 NEE/BSDF power MIS；灯变化失效原始/时域历史，Cornell Box 已改用解析顶灯。Debug/Release 双后端面光源 GPU 专项各 3/3，Release 原有双后端光追场景回归各 1/1；两配置场景与常量打包 CPU 契约通过；无面光源旧场景双后端 128 spp BMP hash 与接入前相同；Release Editor 隔离短测双后端 validation errors/资源残留/worker_failed 为零。40² Cornell 无滤波 4096 spp NEE/BSDF 均值 0.221593/0.221921（DX12），四种子 64 spp 相对 NEE 4096 spp 参考 MSE 0.00501/0.03265；该有限场景对照不证明普遍无偏或性能收益。证据 `out/build/area-light-{debug,release}`、`out/area-light-editor-final`、`out/area-no-light-{reference,current}`。限制：两灯上限、非重合单面恒定辐亮度、四段路径与固定偏移；该阶段原始验证不覆盖光栅与混合光追，发光 Mesh/环境不做重要性采样，未做 ReSTIR、跨设备与长期运动验收。接口见 [面光源](../docs/area-lights.md)，操作见 [Cornell Box](../sample/m4/README.md#cornell-box)。M8 整体与并行时域/NRD 工作仍按各自状态推进。

- Cornell Box 示例已由发光网格改为解析矩形顶灯；原始网格灯验证记录见 Git 历史，当前验证与限制见上述面光源结果。

- M7 项目切换子项 DONE（2026 年 11 月 10 日）：项目 scope、File/Open Project、最近项目和失败回退；托管 Debug/Release 各 26/26，回退/取消为模拟测试；Debug/Vulkan 既有 23 项窗口自检通过（30 份关闭账本零泄漏/验证错误/worker_failed），Release/DX12 可见窗口通过文件选择器切换至 PBR 并显示最近列表。Vulkan 的项目切换、真实加载失败回退与混合 DPI 尚未实机验证；M7.2f 原有人工验收限制保留。使用既有原生构建树，未重建并发 M8 工作。操作和契约见 [手册](../manual/build.md#avalonia-嵌入原型) 与 [工作台](../docs/editor-workbench.md#项目会话与切换)。

- 当前实施阶段：M8 路径追踪收敛与白点修订 IN_PROGRESS（2026 年 11 月 9 日用户反馈重新打开）；B4 继续暂停，M7 其它工作仍为 DEFERRED。
- 当前工作项：为动态运动引导设计按视图成功提交发布的实例变换历史，覆盖失败、实例增删及视图交错；仅按几何一致性保留静态表面历史的候选在重复与连续采样两次单轨迹试验中均因变化区域退化而撤回；继续验证两点漫反射限幅在真实交互和更多场景中的偏差，改善采样并研究成熟降噪器接入；可见物体运动的全局历史失效与静止恢复已完成有限双后端回归；三种子线性 HDR 参考与有限运动对照已完成；首个时域差值候选因运动端点退化撤回，基于空间滤波分量的预览已通过有限对照；NRD 只完成接入缺口评估；按材质调整 BSDF 混合概率的候选因固定场景误差未改善而撤回。B4.5b 仍为 DEFERRED、默认 OFF；B4.5a 的 Debug 窗口超时仍需跟踪。
- 动态实例变换基线 DONE（2026 年 11 月 12 日）：RTX 5060 Release DX12/Vulkan 路径场景回归验证连续成功帧中球体的前后矩阵及物体空间点映射；`scene.contracts` 通过，覆盖提取失败与实例重新出现。日志 `D:/Personal/RenderLab/out/path-transform-validation/<backend>.log`。这是 CPU 变换与有限平移检查，不是逐像素运动矢量；提取历史不等于按视图成功提交的历史，失败提交、多视图交错及动态光照仍待处理，见 [路径追踪](../docs/path-tracing.md#运动预览)。
- 动态历史候选的身份区域对照 DONE（2026 年 11 月 12 日）：RTX 5060 Release DX12/Vulkan 单球单轨迹固定首样本测试中，露出区域线性 HDR MSE 略增，覆盖及其余区域变化很小；证据不足以启用候选，实验已撤回。单种子、有限参考和仅主可见性等限制及日志见 [降噪候选评估](../docs/performance/m8-denoiser-evaluation.md#按首样本身份划分的候选对照)。
- 路径主射线身份诊断子项 DONE（2026 年 11 月 12 日）：默认关闭，按需读回首样本网格、解析灯或环境身份及表面引导；RTX 5060 Release DX12/Vulkan 完整场景回归与面灯专项通过，验证错误为零，移动球体识别 53 个露出和 68 个遮挡像素，诊断开关不改变首帧显示。身份仅限首样本与同帧快照，不代表阴影、间接可见性或持久对象 ID；动态物体历史复用仍未解决。契约和验证入口见 [路径追踪](../docs/path-tracing.md) 与 [降噪候选评估](../docs/performance/m8-denoiser-evaluation.md)。
- 编辑器 Decimal 自动化修复 DONE（2026 年 11 月 10 日）：保留 RangeValue 接口并以 Double 发送数值事件，Debug/Release 托管测试各 27/27；真实 Windows UI Automation 客户端对 DX12 线程数控件写值并收到 Double 事件，30 秒短测正常退出；Debug 双后端完整 smoke 通过，见 `out/m8-uia-client-third`、`out/m8-uia-diagnostic`。此前重试存在焦点/鼠标捕获和尺寸合并断言间歇失败（`out/m8-uia-fix*`）；已增加尺寸失败诊断，原因尚未确定，不能将本次通过视为稳定性证明。30 秒短测不替代 M7.2f 长时验收。
- 用户约定：每个子阶段验收后精简计划、标注 DONE 并提交；暂停检查点不冒充子阶段完成。恢复或切换阶段时先更新本入口，不因整个计划存在就自动执行全部阶段，也不以引入依赖本身作为性能成果。

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
| B4 | 基础库、CPU 计算与 GPU 内存分配优化 | B3、M6 核心 | DEFERRED（用户暂停；B4.1–B4.4、B4.5a DONE，b 实验默认关闭） |
| M5 | Deferred 与光追基础 | M4 | DONE |
| M6 | GPU Driven 与执行优化 | M4；光追调度测试另需 M5 | DONE（M6.1–M6.5；可选 M6.0 延期） |
| M7 | C# 编辑器与完整项目工作流 | M4；默认在 M5、M6 的核心目标后开展 | DEFERRED（已有实现保留，M7.2f 人工复验未关闭；可另行选定子阶段，不依赖 B4 实验完成） |
| M8 | 一个深入的渲染专题与作品交付 | 所选专题依赖已满足 | IN_PROGRESS（用户反馈收敛慢与白点严重） |

默认顺序为 M0 到 M8。M5 和 M6 在 M4 后可以独立推进；单阶段内按下列子阶段顺序执行，不同时建设多个高级系统。完整编辑器后置，M4 只实现运行内容所需的最小项目与资源能力。

R1 契约修订、B1 样例整理/Benchmark、B2 调查和 B3 资源复用已完成，均按子阶段验证与提交。

### 暂停后的工作入口

- 继续 B4：直接从 B4.5b 检查点恢复，不重做已完成 a，不先改 Vulkan；恢复顺序见下方。
- 切换 M7：当前桥接 ABI 为 v11，默认场景/原生宿主/工作台可复用；原生构建用默认 OFF。
  `M7.2f` 继续真实输入、画质与混合 DPI 验收；如选择 `M7.3`，从原生材质事务/Undo 边界和 Schema Inspector 开始，不自动扩展成场景或资源编辑。
- 切换 M8：M4–M6 核心前置已满足，可独立于分配器实验开始；仍须先选一个主专题，不能将四个候选同时启动。
- 其它工作共用当前 checkout 时，不依赖忽略目录 `out` 中的草稿或旧 DLL。原生/托管使用同配置；
  已验证构建树为 `out/build/m5-{nmake,release}`，编辑器脚本显式传对应 `-NativeBuildDir`。常规命令见 [手册](../manual/build.md)。

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

状态：DEFERRED，2026 年 10 月 30 日按用户要求暂停并保存检查点。已完成部分保留简短验收记录，未完成部分在恢复授权后依次推进；不因计划存在而自动建设完整资产管线、通用 Job System 或新渲染功能。

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

状态：DEFERRED（b 用户暂停；a DONE）。恢复后按 a–c 边界独立验收和提交，不同时改两个后端的分配实现及 Graph Alias 算法。

a 状态：DONE，2026 年 10 月 30 日。范围/保留块字节与数量分离，统一限额约束 reserved；
普通与 Alias 共用账本，驱动 segment/heap 与进程内存独立导出，未知为 null，热路径不查询 OS。
验证：两配置 clean-first 全目标构建；Release CTest 66/66，Debug 65/66（Vulkan 光追窗口超时）后
该用例隔离连续 3/3；托管各 22/22、Python 15/15，两起始后端工作台各 23 项及各 30 次关闭四项计数全零。
双后端 PBR 参考 hash 不变、driver budget 实际可用，validation errors=0。契约见 [GPU 内存](../docs/gpu-memory.md)，
数据与未复现超时限制见 [验证](../docs/performance/b4-memory.md)，证据 `out/b4/memory-accounting`。
限制：本步尚未接入分配库，未进行其它 GPU/长期压力；首轮窗口超时不被复验隐去。

#### b 检查点与恢复顺序

状态：DEFERRED，未完成验收。实验开关默认 OFF；默认构建不下载/链接 D3D12MA，
不把试验路径作为其它阶段的前置或生产默认。

已保存：D3D12MA 3.1.0 / `0fa62ed3a0a69b73230a8ec1faa752d4061c8dc8` 固定依赖；
`dx12_memory.cpp/.hpp` 的具体适配层、16 MiB 固定池、整块准入、committed 回退、
资源/块分离计费和仅回收空闲池的 `Runtime::TrimMemory()`；上传、描述符和原生 Alias 路径保留。
普通资源拥有库 allocation，命令和完成点继续保活原资源；关闭时显式 Trim。

实际验证边界：开关隔离前，Release 的独立 `renderlab_gpu_memory_tests` 初次通过一次，
覆盖共享块/映射、小预算回退、拒绝不改计数、取消/提交保活与回收、超块大小资源。
这不是最终开关 ON 的整体验收。原 b 的 Release/Debug 全目标构建被用户中断，不能记为成功；
基线测量目标已构建，但尚未运行成对测量、完整 GPU/窗口/编辑器回归或许可证安装检查。
暂停收尾恢复 OFF 后的构建/检查另见本次检查点记录，不转记为 ON 通过。
检查点：OFF 模式的 Debug/Release 全目标构建成功，各 24/24（20 项 CPU/CLI + 双后端
Alias/RHI 共 4 项）通过；两棵日常构建树均已恢复 OFF，格式、UTF-8/LF 和 Git 差异检查通过。
证据 `out/b4/dx12ma/checkpoint-default-{debug,release}-{build,tests}.log`；
没有在暂停收尾中重跑完整窗口/托管验收或运行性能对照。

恢复时：

1. 阅读 [内存契约](../docs/gpu-memory.md)、`src/rhi/dx12/dx12_memory.cpp` 和
   `tests/gpu_memory_tests.cpp`；确认普通构建仍 OFF，再按 [试验命令](../manual/build.md#dx12-分配器试验入口) 使用独立构建目录开启 ON。
2. 先完成两配置构建和 `gpu.dx12.memory`、CPU/Graph、双后端 RHI/Alias。
   重点审查失败后空闲块保留/回收、整块硬预算、范围销毁先于块释放、tight alignment 关闭及 AS 地址对齐，
   不以初次单用例通过替代这些项。
3. 再跑索引/间接、多队列/并行、AS/Ray Query/反射、PBR/窗口 Resize 和工作台后端重建；
   首轮 a 的 Debug/Vulkan 光追窗口超时需串行复查并保留结果，不简单延长超时掩盖问题。
4. 用同一测量工具对照 `36f6275` 的 committed 基线与 ON：冷启动/动态创建释放、稳定帧、
   allocated/reserved、块数/峰值/空闲回收；固定帧图像与关闭四项计数必须核对。
   `out/b4/dx12ma/baseline-a-bin-36f6275` 是保存的旧 Player/Sandbox，
   `baseline-a-source-36f6275` / `a-measure-build` 是忽略的源快照与测量目标，不是权威源码。
   若 out 缺失，用 `git archive 36f6275` 重建，仅加入同一测量源码/目标并定义 `RENDERLAB_MEASURE_ONLY=1`，
   记录真实 exe/source hash；不要在主 checkout 来回切版本或回退他人的修改。
5. 完成数据、文档、安装检查后才将 b 标 DONE 并提交。是否默认启用由正确性和实测决定；
   无足够收益可以保留 opt-in/延期，不因依赖存在就强行启用。之后才开始 c。

- [ ] c：固定版本 VMA 并接入 Vulkan 普通资源，处理映射、coherent 要求、device address/AS、分配对齐与销毁顺序；保留 Alias 原生路径的合法性与同步。
- [ ] 两后端 Debug/Validation、Release、Alias/多队列/光追及编辑器重建回归；对比加载、动态分配、稳定帧、底层块数/峰值/空闲回收。关闭后存活分配与最终保留块分别检查，不能将缓存容量隐藏为零。
- [ ] 首轮不实现 defragmentation、资源搬迁或自动驻留系统，不宣称减少 Shader 工作量或纹理本体大小。

### B4.6 资产处理库与整体验收

状态：NOT_STARTED。

- [ ] 评估 meshoptimizer 的导入后 cache/fetch 优化，保持顶点属性、三角形绕序、子资源身份和缓存版本；采用时覆盖光栅/光追及退化网格。
- [ ] 对照 cgltf 与当前解析、DirectXTex 与当前纹理契约，给出采用边界；完整 importer 迁移、mip/BC 的 RHI 扩展仅在具体需求和验证支持时实施，否则明确 DEFERRED。
- [ ] 汇总实际采用/未采用库、基线对照和限制；更新 docs/manual/总体设计，完成相关原生与托管回归。所有已采用路径验证完成才标记 B4 DONE；延期能力不算已实现。

## M7 C# 编辑器与完整项目工作流

本轮状态：DEFERRED，尚未指定恢复哪个子阶段；不依赖默认关闭的 B4.5b 实验完成。
以下实现证据保留；M7.2/M7.2f 中的 IN_PROGRESS 表示未关闭的既有验收，不代表当前正在实施。

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

- [ ] 按 [分层设计](architecture.md#总体分层) 从实际材质用例抽取原生应用服务及桥接；核心保持可独立使用，编辑专用服务不强加给 Player。
- [ ] Schema 自动 Inspector、未保存材质预览、参数验证。
- [ ] Begin/Update/Commit/Cancel 编辑事务，连续拖动形成一条 Undo。
- [ ] UI 快照和命令队列不直接暴露 GPU 对象，编译事件显示错误位置。

### M7.4 项目与资源操作

状态：DEFERRED（本轮范围外，原 M7.3）。

- [ ] 项目会话与资源操作由原生应用服务维护权威规则，C# 负责界面和状态投影；按实际用例扩展桥接。
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

状态：IN_PROGRESS。基础路径追踪、编辑器接入和显示降噪曾完成；用户反馈高样本仍有严重
噪点与白点，重新打开以改进采样和收敛，其余候选未实施。不得把低样本显示滤波的改善当作
积分器收敛已解决。

- [x] 建立约 19948 spp 未滤波基线；受控拆分表明间接路径上的方向光贡献是主要方差来源。
- [ ] 改进采样，保留可对照的原始结果；验证能量、收敛、图像偏差与双后端。按材质调整 BSDF 混合比例候选已撤回，原因见 [对照记录](../docs/performance/m8-path-denoise.md#bsdf-采样比例候选已撤回)。
- [ ] 完整验证快速运动、遮挡及球体高光序列和真实窗口等待时间；研究分离漫反射/镜面信号和间接光采样后再关闭阶段。

多种子运动基线（2026 年 11 月 10 日）：增加可复现采样种子并在种子变化时重置
渐进和时域历史；默认种子保持既有流。RTX 5060 上 Debug/Release 的 DX12/Vulkan
`gpu.*.reflections` 均通过，覆盖种子变化、复现和历史重置。Release 三条轨迹各三个
候选种子对照另一种子 4096 spp 参考，时域全序列 MSE 略低而停帧 MSE 略高；参考、
8 位度量和场景限制见 [评估](../docs/performance/m8-denoiser-evaluation.md#多种子运动基线)。

三种子线性 HDR 运动参考（2026 年 11 月 10 日）：新增可选显示分支 HDR 读回，
三种子各 4096 spp 在线性空间平均并输出 PFM；RTX 5060 Release DX12/Vulkan 的
`gpu.*.reflections` 各通过 1/1，九组全序列及停帧线性 MSE 时域均略低于空间结果，
多数差距很小。独立脚本复算一组指标并核对参考均值；Debug、动态物体与主观画质未
验证，方法、数据和有限参考限制见 [评估](../docs/performance/m8-denoiser-evaluation.md#三种子线性-hdr-参考)。

路径诊断与面光源阴影合并复验（2026 年 11 月 12 日）：RTX 5060、Release 下
DX12/Vulkan 的路径场景、面光源渲染、光栅面光源和阴影滤波 GPU 专项共 8/8 通过；
构建和测试入口为 `out/build/area-light-release`。本次未重跑 Debug、编辑器窗口或
人工画质验收，原有设备和动态画质限制保留。

移动几何历史回归（2026 年 11 月 12 日）：RTX 5060、Release 下 DX12/Vulkan 的
光追场景回归通过；四帧可见球体移动均重置全局渐进与时域历史，时域开关的冷启动
HDR 相同，停下后重新累积；两后端首末画面各有 207 像素超过指定变化阈值。首次选择
的球体可见变化不足，已改用前排球体；该结果不构成遮挡真值、动态区域复用或主观
画质验收，详见 [评估](../docs/performance/m8-denoiser-evaluation.md#移动物体历史失效)。

移动几何历史保留候选（2026 年 11 月 12 日）：仅用几何匹配保留显示历史的候选在
Release/DX12 单轨迹、单种子对照中全图改善极小，参考变化通道误差增加，已撤回；
回滚后 Release/DX12 场景回归通过，八张移动帧截图与候选前逐字节一致；未验证候选
的 Vulkan、其他轨迹或性能，方法和证据见
[评估](../docs/performance/m8-denoiser-evaluation.md#物体运动时保留历史候选已撤回)。

连续采样的移动历史候选（2026 年 11 月 12 日）：试验中令空间基线与候选共同推进
相同的逐帧采样序列，避免反复使用相同随机起点；Release/DX12 单轨迹对照整体收益不足 1%，变化区域
误差升高，再次撤回。恢复基线并重建；候选未做 Vulkan、多种子或性能验证，方法见
[评估](../docs/performance/m8-denoiser-evaluation.md#物体运动时保留历史候选已撤回)。

两点漫反射亮点限幅（2026 年 11 月 10 日）：定位到球体亮点因只有两个匹配样本而
绕过滤波，对明显超限的漫反射增加保守限幅并保留两点镜面；RTX 5060 Release 的
DX12/Vulkan 场景专项通过，固定 HDR 热点及镜面检查通过，三轨迹三种子线性 HDR
对照整体改善但部分受影响通道变差；固定静止 504/8184 spp 截图与旧版一致。
Release 编辑器双后端三秒 Gallery 短测正常关闭且零验证错误/剩余分配/worker_failed，
见 `D:/Personal/RenderLab/out/hdr-guarded-editor-{dx12,vulkan}`。叠加 LTC 光栅面光源后，
Release 的 DX12/Vulkan 光栅面光源、路径面光源和路径场景专项均通过，编辑器双后端
Gallery 三秒短测零验证错误/剩余分配/worker_failed；证据 `D:/Personal/RenderLab/out/hdr-ltc-*`。
Debug、真实交互及更多设备未验证，局部偏差与成本见
[评估](../docs/performance/m8-denoiser-evaluation.md#两点漫反射亮点限幅)。

合并复验（2026 年 11 月 12 日）：面光源基线与两点限幅合并后，RTX 5060、Release
下 DX12/Vulkan 的路径追踪、光栅面光源及路径面光源 GPU 专项共 6/6 通过；构建树与
结果为 `out/build/m5-release`。本次未重跑 Debug、编辑器窗口或人工画质验收，既有
限制继续保留。

分量时域差值候选（2026 年 11 月 10 日）在 Release/DX12 首项运动端点测试退化并已
撤回；重建后 Release/DX12 专项通过，312 张序列图片与候选前逐字节一致；未运行
Vulkan 或多种子候选画质测试。方法、结果和后续方向见
[评估](../docs/performance/m8-denoiser-evaluation.md#分量时域修正候选已撤回)。

空间滤波分量诊断（2026 年 11 月 10 日）：按需输出漫反射/镜面与滤波总量，验证与
直接分量重组；RTX 5060、隔离 Release 的 DX12/Vulkan 场景专项通过，诊断开关的首帧
显示图相同；日志 `D:/Personal/RenderLab/out/pc{2,3}`。只覆盖当前场景，未做独立 GPU
合成边界或性能测试；后续已用于分量时域候选并通过组合后的 Debug/Release 回归，见
[契约](../docs/path-tracing.md)。

分量时域预览（2026 年 11 月 10 日）：分别累积空间滤波后的间接漫反射与镜面，
镜面使用保守视差置信度；RTX 5060、隔离 Release 的 DX12/Vulkan 场景专项通过，
独立历史读回覆盖长度和有效数值，三轨迹各三种子对照及短时成本见
[评估](../docs/performance/m8-denoiser-evaluation.md#分量空间结果的独立时域预览)。
Release 托管编辑器配隔离 Release Bridge，双后端 Gallery 3 秒短测正常关闭，零验证
错误/剩余分配/worker_failed，`D:/Personal/RenderLab/out/pc-editor-{dx12,vulkan}`；
叠加矩形面光源接线后，隔离 Debug/Release 的 DX12/Vulkan 场景专项和 Release 面光源
HDR 专项均通过；Release 托管编辑器与隔离 Bridge 双后端 Cornell Box 三秒短测，零
验证错误/剩余分配/worker_failed（`D:/Personal/RenderLab/out/pc-{debug,area-combined,area}`、
`D:/Personal/RenderLab/out/pc-combined-editor-{dx12,vulkan}`）。尚未做完整 smoke、
动态物体、用户交互画质或多种子 HDR 参考验收。

首个次级距离与镜面视差引导（2026 年 11 月 10 日）：按主表面匹配和采样分布汇总
首条次级距离，低粗糙度时以近似视差保守调整混合历史置信度；不改变原始累积，也不
等同完整镜面重投影或 NRD 输入。RTX 5060 Debug/Release 双后端 GPU 回归各 4/4，
Debug Vulkan 编辑器完整 smoke 与双后端 Path Tracing 窗口短测通过；DX12 完整 smoke
两次因 Avalonia `UnsupportedType (Decimal)` 失败，后续修复并重新通过，见当前状态。固定轨迹的指标
变化很小，不能宣称明显画质改善；方法、成本和失败堆栈入口见
[降噪验证](../docs/performance/m8-path-denoise.md#首个次级命中引导与镜面置信度)。

主散射间接分量（2026 年 11 月 10 日）：相同路径按主散射的 BRDF 两项逐通道分配
间接贡献，独立累积漫反射，以总量减直接和漫反射得到镜面；各分量分别空间限幅，
保持原始总历史与现有开关。RTX 5060 Debug/Release 双后端 GPU 数值及场景回归
各 4/4、Debug 双后端编辑器 smoke 通过；半球积分覆盖金属/非金属、两粗糙度与多个视角，
场景 HDR 读回检查非负与切换一致。固定场景画质改善有限并仍有偏暗，额外成本与
镜面半强度候选退化见 [降噪验证](../docs/performance/m8-path-denoise.md)。尚无每分量
时域、次级 hit-distance、NRD 接入或跨多种子验收，不将此项当作降噪终点。

降噪候选评估（2026 年 11 月 10 日）：额外间接光宽尺度在高样本静止图改善，
但低样本与运动球体区域 MSE 退化，已撤回并确认回滚图与原结果一致。NRD 只读
评估了输入、资源与许可入口，尚未接入、构建或运行；见
[候选评估](../docs/performance/m8-denoiser-evaluation.md)。

主表面直接光分离（2026 年 11 月 10 日）：直接贡献单独累积，显示滤波只处理剩余
间接分量并加回直接量；原始总累积与开关不受影响，亮度统计仍为总信号。RTX 5060
Debug/Release 双后端 GPU 场景及合成测试通过，Debug 编辑器双后端 smoke 通过。
固定场景与运动轨迹指标、偏差和成本见 [降噪验证](../docs/performance/m8-path-denoise.md)。
间接漫反射/镜面尚未分离，多尺度 SVGF、NRD 对照和遮挡的真实窗口验收仍未完成。

路径追踪运动预览与序列复核（2026 年 11 月 10 日）：独立显示历史按主表面与材质重投影，
按屏幕位移降低权重、拒绝低支持度历史并加快停止后的交接，保留原始累积和开关。
RTX 5060 的 Debug/Release 双后端 GPU 场景回归及 Debug 双后端编辑器 smoke 通过；
慢速、快速和横移轨迹逐帧对照在固定场景有小幅改善，停止后仍有少量误差增加。
图像、成本与限制见 [降噪验证](../docs/performance/m8-path-denoise.md)。当前未分离
镜面信号，未确认遮挡真值、真实窗口与用户主观画质，不能认为球体噪声和拖影问题已解决。

编辑器缺失 Shader 修复（2026 年 11 月 10 日）：托管构建检查原生构建树是否包含当前
全部内置 Shader，缺失时提示重建 Bridge；检查不判断现有文件或 DLL 内容是否过旧。实测
旧构建树正确报错，重建 Debug Bridge 和托管程序后 RTX 5060 上 DX12/Vulkan 编辑器
自检均通过（`out/m8-debug-shader-fix`）；默认 Release 构建树和托管构建已更新。

批量采样与显示滤波修订：每帧最多 8 条独立路径、按真实样本数合并 HDR 历史，
显示滤波按亮度统计和样本数渐弱；不改变积分器分布，未声称消除原始高方差。RTX 5060
Debug/Release 双后端 GPU 回归各 4/4、Release 编辑器双后端 smoke 通过；
新版独立包解压后双后端 validation 启动通过（`out/RenderLab-PathTracing-batch.zip`）；对照与短测见
[降噪验证](../docs/performance/m8-path-denoise.md)。

结果：独立 PathTracing Graph，内置不透明 Lit/Unlit、四段路径、金属粗糙度漫反射与
visible-GGX、方向光/环境/发光及未滤波 HDR 累积；ABI v11 的编辑器支持管线选择、能力
校验和持续累积。默认引导空间滤波仅处理显示、保留原始历史，可关闭，并随统计不确定度和样本数渐弱。
滤波有偏，会软化细节/真实高光。接口与操作见 [路径追踪](../docs/path-tracing.md)、
[编辑器宿主](../docs/editor-host.md) 和 [手册](../manual/build.md)。两套场景、运行包、录像、
RenderDoc Capture 和技术记录见 [初版验证](../docs/performance/m8-path-tracing.md)；新的包为
`out/RenderLab-PathTracing-demo.zip`，旧录像与 Capture 对应未滤波初版。

验证：MSVC Debug/Release 全目标构建，最终初始化修正后重建 Sandbox/Player/Bridge；RTX 5060 上两配置的 DX12/Vulkan Graph、BSDF/AS 与
场景回归各 5/5，验证滤波开关不重置或污染原始历史。两配置托管测试各 22/22、双后端
工作台自检各 23 项、各 30 次关闭账本和 validation errors/worker_failed 均为零。
旧版本 Release 双后端高样本开/关图 hash 各自一致（当前滤波策略不再保证 2048 spp 精确旁路）；两个固定场景的低样本对照、异常亮点指标、
内存、成本与局限见 [降噪验证](../docs/performance/m8-path-denoise.md)，记录在
`out/m8-denoise`、`out/m8-denoise-editor{,-debug}`；最终初始化修正后的 Release 双后端工作台
自检另见 `out/m8-denoise-final-editor`。初版 CPU 独立 BSDF 积分、双场景多尺寸、
窗口、独立解压包与 Capture 的结果见初版验证；旧版更新包已解压后双后端启动验证；本轮独立包另见上述 batch 包。

限制：只在本机 RTX 5060 实测，未在第二台机器运行；不支持透明、Masked、动态几何或通用
发光体重要性采样。四段上限、固定偏移、单 mip、有限采样仍有误差与噪声；引导空间滤波有偏，
可能模糊高光，抖动引导在边缘可能闪烁。未验证其他 GPU、4K、长期压力、Vulkan Capture、
用户主观画质或 M7.2f 的真实输入/混合 DPI。演示未托管公共站点；`out` 交付物不纳入 Git，
需另行保存。
