# RenderLab 阶段实施计划

本文件是实施状态的唯一记录入口。[architecture.md](architecture.md) 定义长期架构，[design-review.md](design-review.md) 解释本轮复查的取舍。阶段按可运行结果验收，只有设计或代码存在都不足以认定完成。

## 当前状态

- 当前实施阶段：M0–M4 DONE；M4.1–M4.5 全部完成。
- 当前工作项：R1 IN_PROGRESS，R1.1 已完成（a1147d5/f09c321），R1.2a 已提交 65879ad；R1.3a 参数契约/代码生成基础已完成，下一工作项 R1.3b 场景绑定与显式 HLSL 库迁移；M5 NOT_STARTED。
- 用户约定：每个子阶段验收完成即提交 Git，无阻塞时在当前授权范围内继续推进；M4.3 提交 c50ee6a、M4.4 提交 40d0c12、M4.5 提交 9a4b2e4。
- Demo 的 Custom 示例作为回归入口保留；M4 场景示例使用 Forward 组合与现有 Shader/Material 链路。

| 阶段 | 目标 | 前置依赖 | 状态 |
| --- | --- | --- | --- |
| M0 | 工程与设备基础 | 无 | DONE |
| M1 | 最小双后端 RHI | M0 | DONE |
| M2 | Shader 与材质数据链路 | M1 | DONE |
| M3 | 单队列 Render Graph | M2 | DONE |
| M4 | 场景资源与基础 Renderer | M3 | DONE |
| R1 | Shader/Material 契约修订 | M4 | IN_PROGRESS |
| M5 | Deferred 与光追基础 | M4 | NOT_STARTED |
| M6 | GPU Driven 与执行优化 | M4；光追调度测试另需 M5 | NOT_STARTED |
| M7 | C# 编辑器与完整项目工作流 | M4；默认在 M5、M6 的核心目标后开展 | NOT_STARTED |
| M8 | 一个深入的渲染专题与作品交付 | 所选专题依赖已满足 | NOT_STARTED |

默认顺序为 M0 到 M8。M5 和 M6 在 M4 后可以独立推进；单阶段内按下列子阶段顺序执行，不同时建设多个高级系统。完整编辑器后置，M4 只实现运行内容所需的最小项目与资源能力。

用户已授权复查后实施 R1，按子阶段验证、提交并继续；M5 不在本轮实施范围。

## 状态与维护规则

状态使用 NOT_STARTED、IN_PROGRESS、DONE、BLOCKED、DEFERRED。通常只保留一个 IN_PROGRESS 主阶段，并在当前状态标出具体子阶段。BLOCKED 必须写明阻塞条件与恢复所需事项；DEFERRED 表示明确推迟，不能算作完成。

开始工作时更新当前阶段和工作项，按本次用户授权推进。任务复选框只表示子项完成，主阶段必须满足全部退出条件。代码未运行、测试未执行、硬件功能未验证时，直接写明，不能把“应该可用”记为通过。

每完成一个子阶段，在同一次文档更新中：

1. 核对该子阶段的交付和验收，记下实际使用的构建配置、后端和设备。
2. 把已完成子阶段的详细待办替换为简短完成记录，保留结果、验证入口及限制。未完成的后续部分继续保留详细计划。
3. 将长期接口说明放入 docs，构建运行步骤放入 manual；计划只链接入口，不复制整份说明或测试日志。
4. 更新主阶段状态和下一工作项。整个主阶段验收后，把全部详细步骤压缩为一个完成摘要，保留稳定的阶段标题与编号。

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

## M0 工程与设备基础

状态：DONE，日期：2026 年 10 月 23 日。

结果：工程规范、C++20/CMake 固定依赖、日志/断言/原生窗口/参数，以及 DX12/Vulkan 设备与能力报告。

验证：RTX 5060 上 Debug/Release 各 9/9，双后端 Validation 错误为 0；Intel 显式设备选择和窗口生命周期通过。后续头文件依赖修复见 M3.6；光追仍仅查询能力。

入口：[基础模块](../docs/m0-foundation.md)、[构建运行](../manual/build.md)、[工程约定](../manual/conventions.md)。

## M1 最小双后端 RHI

状态：DONE，日期：2026 年 10 月 23 日。

结果：交换链与两帧上下文、真实完成点与录制保活、资源/上传/读回、不可变绑定与 PSO、光栅/Compute/间接绘制、Reversed-Z/sRGB、Marker/Timestamp。

验证：RTX 5060 上 Debug/Release 各 13/13，双后端 Validation 错误为 0；固定噪声逐字节参考、取消/退休、池与上传复用、resize/minimize/restore 和退出通过。后续标准目录回归见 M3。

边界：单 Graphics Queue、单 mip/layer、单流顶点、有限纹理格式、committed/dedicated 分配；没有 Alias、Bindless、设备丢失恢复或其他 GPU 的完整验收。完整约束见 [RHI](../docs/m1-rhi.md)，运行入口见 [构建手册](../manual/build.md)。

## M2 Shader 与材质数据链路

状态：DONE，日期：2026 年 10 月 23 日。

结果：.shader/.params 声明、DXIL/SPIR-V 编译和内容缓存、固定布局/反射/Schema、材质编辑保存与不可变 GPU 快照、有界 PSO 缓存及后台热重载/帧边界发布。

验证：RTX 5060 上 Debug/Release 各 16/16；最终追加的材质热重载专项双后端均通过，Validation 错误为 0。后续完整回归见 M3。

边界：无编辑器/文件监视器；SM 6.0/Vulkan 1.3、有限绑定/变体和数值布局；无资源数组、任意结构化元素或跨机器发行验收。入口：[Shader/材质契约](../docs/m2-shader-material.md)、[构建运行](../manual/build.md#shader-与材质工具)。

## M3 单队列 Render Graph

状态：DONE，日期：2026 年 10 月 24 日（含标准构建修复复验）。

结果：类型化版本句柄、名称资源声明、Raster/Compute/Copy Builder 与受限 Execute、RAW/WAR/WAW/剔除/拓扑排序/屏障、Import/Export、有界池；RenderFeature 实例、注册后 Create 依赖解析、Custom/Forward 组合及真实阶段快照。

验证：RTX 4060 Laptop GPU、MSVC 19.51、DXC 1.8，最终标准 windows-debug 完整 CTest 19/19；DX12/Vulkan 窗口直接启动与图像/噪声/Buffer/快照参考通过，验证错误为 0。原接口曾在 RTX 5060 上通过 Debug/Release 各 19/19；新接口未复验 Release 或其他 GPU，历史结果不替代新配置验收。

边界：单队列/单线程/单视图、单 mip/layer、单颜色附件与固定 Store；无 Alias、多队列、计划缓存或深度采样；可选逐 Pass GPU Timestamp 后由 M4.5 补齐。Feature 依赖显式声明，Full 写覆盖由作者保证；普通 Forward 目前消费已准备 DrawPacket，场景、Material 适配和光照属于 M4。

入口：[Graph API](../docs/m3-rendergraph.md)、[Feature/Create/Forward](../docs/forward-pipeline.md)、[构建与恢复](../manual/build.md#render-graph-与-feature)。产物位于构建树 graph、logs、Testing，不纳入源码。

### M3.4 简化 API 与普通 Forward 组合

已完成：名称查询、直接 Builder 与统一 Execute；Opaque/Transparent、只读深度和真实快照。接口与验收汇总见上述入口。

### M3.5 Feature 实例与流式管线统一

已完成：Feature 实例及名称连接、无序注册后 Create；依赖缺失/重名/环/阶段冲突与固定组合隔离均验证。

### M3.6 既有构建树依赖修复

已完成：定位 showIncludes 编码导致旧目标文件混用，自动检测前缀并完整重编译标准目录；依赖表与双后端启动已验证。本地 PDB C1902 用缓存中的 /Z7 处理，未写死共享预设；恢复方法见 [工具链](../manual/build.md#工具链)。

## M4 场景资源与基础 Renderer

状态：DONE，日期：2026 年 10 月 26 日。

结果：最小 Project/UUID/资源缓存、静态 glTF/GLB、可保存 Scene/Material、层级变换/相机/主方向光/CPU 剔除；普通 Shader/Material 驱动的 Lit/Unlit、HDR/ACES/IBL、单张方向光阴影、Masked 与排序透明；原生调试面板、Graph 纹理查看、逐 Pass GPU Timestamp、固定配置/性能报告和独立原生 Player。原 Custom/RHI 示例与项目 Copper Shader 保留。

最终验证：RTX 5060、MSVC 19.51.36257、DXC 1.9.0.5512，标准 windows-debug/windows-release 构建及完整 CTest 各 37/37（11 CPU/CLI、26 GPU）通过，DX12/Vulkan validation error 为 0。content/scene/material/graph/RHI 原有回归通过，场景/材质保存重载、窗口 STA/两帧提交/resize/minimize/restore/退出和泄漏检查均覆盖。独立安装目录 out/player-m45-validation 的双后端 headless 与各 4 帧窗口启动通过；Library 从安装内容重建。

图像：两配置各 16 张 PBR 参考图双后端逐字节一致，24 张 surface 专项图最大差异 2 个显示 code；透明线性混合→ACES→sRGB 与独立 CPU 参考误差不超过一个 code。1280×720 Release 基线中 Pbr 有 2 个像素相差一个 code，Shadow 有 19 个像素的局部 PCF 差异、最大 16 个 code，比较数据已保留，不以小尺寸结果代替高分辨率验收。

基线：两场景 × 两后端，Release/validation off、1280×720/Medium、预热 5 帧后各 120 帧。CPU render 均值 1.770–7.635ms，已测 GPU Pass 范围均值合计约 0.037–0.068ms，RHI 当前约 22.88–28.38MiB；进程 working set/private 和逐 Pass/min/max/count 见 [原始记录与范围](../docs/performance/m4-rtx5060.md)。GPU 范围合计不等于含同步/Present 的完整帧耗时。

边界：单 Graphics Queue、单视图、静态三角形、point-clamp、单 mip/layer；低分辨率 CPU IBL 与 LDR 环境源，无 .hdr/.exr；导数切线框架非 authored TANGENT/MikkTSpace 保证。单张阴影，无级联/完全稳定拟合；透明不投影，无相交透明/OIT/折射。项目 Shader 需显式提供 Shadow Pass 才投影；Sky、DepthPrepass、Bloom 按需增加。Vulkan 检查并启用 shaderDemoteToHelperInvocation，保留未消费顶点属性提示；DX12 保留优化 clear 值不匹配的性能提示，无验证错误。仅本 NVIDIA 设备完整验收，新面板的手动键鼠操作/其他 GPU 未验收；原始完整 PBR Debug DX12 交互已由用户确认恢复。

入口：[场景/资源](../docs/m4-scene.md)、[PBR/HDR/IBL](../docs/m4-forward-pbr.md)、[阴影/Alpha](../docs/m4-shadows-alpha.md)、[工具/Player](../docs/m4-tools.md)、[构建运行](../manual/build.md#m4-项目场景)。构建树 pbr、surface、tools 保存图像/Graph/日志/隔离资产与 backend-comparison.json；各配置 Testing/Temporary/LastTest.log 保存最终完整验收。

### M4.1 最小项目与资源

DONE（2026 年 10 月 24 日）：Manifest/挂载根、UUID/.meta/子资源/依赖移动、静态 glTF/GLB/WIC 与可重建缓存；用户材质与导入产物分离。content.contracts 持续回归缓存、身份、依赖和非法内容；PBR/Alpha 导入在 M4.3–M4.4 补齐。

### M4.2 场景与 GPU 数据

DONE（2026 年 10 月 24 日）：层级 Scene/相机/方向光、值快照/剔除/历史/法线与镜像、Material→Forward/Graph；项目 Shader 共用内置 Core/Lighting。相机俯仰使用局部右轴，主光按稳定 UUID 选择；最终 scene/content 与双后端窗口回归覆盖。

### M4.3 Forward PBR

DONE（2026 年 10 月 26 日）：Lit/Unlit、Surface/Math/BRDF、MR/Normal/AO/Emission、RGBA32F HDR/曝光/ACES 与单次 sRGB、确定性预滤波图集和 split-sum LUT；Pbr.rlproject 固定材质球参考。最初 Debug/Release 各 25/25，COM/窗口修复后各 27/27，最终完整回归见本阶段汇总。

交互修复：窗口宿主持有完整生命周期的 COM STA，WIC 解码复用调用者 apartment；用户在 VS Code 终端重跑原始 Debug DX12 完整 PBR 命令，确认蓝圈与交互问题消失。另修复 generation 未驻留 PSO 导致退休帧后重复创建的问题，保留独立生命周期回归。

### M4.4 阴影与透明

DONE（2026 年 10 月 26 日）：BeforeOpaque ShadowFeature、RGBA32F 记录 Reversed-Z 深度与 D32 可见性、3×3 PCF/接收平面修正、相机视野外投影；Lit/Unlit Masked/Transparent variant、glTF alphaMode/alphaCutoff 与 v3 缓存、HDR straight alpha/只读深度/排序。Shadow.rlproject 为固定参考；子阶段 Debug/Release 各 32/32，最终完整回归见汇总。

### M4.5 可重复演示

DONE（2026 年 10 月 26 日）：同线程 modeless Win32 面板、F1、冻结/重置相机、曝光/阴影质量、最终/HDR/阴影/环境/LUT 查看；Graph 可选逐 Pass Timestamp，64 slot/最多 32 个存活 Pass，完成后读取。固定宽高/质量/项目与 5 帧预热测量，独立 Player 与安装目录，共用现有宿主和渲染流程。

补充修复：Graph/Pool 改用真实 FormatBytes 估算，修正 HDR 按 4 字节低估的问题；SceneRenderer 的池上限明确设为 64 项/224MiB，RHI 仍独立检查 256MiB。CPU 回归覆盖格式预算/溢出/未完成计时读取；双后端验证连续提交的 2048² 阴影、计时不改变画面、查看器图剔除和控件命令/布局边界。最终 Debug/Release 各 37/37 与固定基线见汇总；不把自动控件测试当作手动 UI 验收。

## R1 Shader/Material 契约修订

状态：IN_PROGRESS；设计复查通过并开始实施，日期：2026 年 10 月 26 日。

设计入口：[Shader 与材质 v2](shader-material-v2.md)。复查明确附件格式独立于测试/写开关、无测试但写深度映射 Always、Graph 先保守跟踪组合附件；先做 RHI/Graph，再接作者层，避免引入临时材质状态 DSL。M4 基线必须在迁移时持续回归。

### R1.1 类型化 Pass 与固定功能状态

R1.1a DONE（2026 年 10 月 26 日）：类型化 Raster/Depth/Blend/Stencil、完整 PSO Key、动态 StencilRef、D32S8 双后端映射与能力查询；Graph 逐面 Load/Clear/权限及整附件保守依赖；Forward 不再强制透明禁止深度写入。接口：[渲染状态](../docs/render-states.md)。RTX 5060 / MSVC 19.51 / DXC 1.9，Debug/Release 全量各 39/39；追加 Fail/DepthFail/非法状态后两配置定向各 3/3，DX12/Vulkan Validation 错误为 0。格式化、UTF-8/LF 与差异检查通过。限制：无深度模板采样/复制/独立平面调度。

R1.1b DONE（2026 年 10 月 26 日）：显式 role/代际 PassHandle、完整 state 作者块/名义类型属性引用、有限配方与覆盖、Surface/Blend/AlphaClip 分类和队列；导入默认 Cull 与作者覆盖分离，镜像只调整 FrontFace。Lit/Unlit 改用静态 AlphaClip 与普通 Surface/Blend 属性，入口显式编码混合输出；SceneRenderer 准备 PSO 前按 camera/shadow 汇总模板格式需求。文档：[渲染状态](../docs/render-states.md)。

R1.1 验收：RTX 5060，Debug/Release 全量各 39/39；追加严格作者检查后 Debug 定向 6/6。双后端场景在任意 Pass 改名并启用 camera/shadow 恒等 stencil 后像素一致；状态修改复用 Shader 模块、角色歧义/过期 handle/错误属性类型诊断通过，Validation 错误为 0。R1.1 整体 DONE；参数绑定名称与旧变体资源格式的后续修订仍分别归 R1.3/R1.4。

### R1.2 类型化属性与静态轴

状态：IN_PROGRESS；R1.2a 类型化属性/静态轴基础 DONE（2026 年 10 月 26 日），随后接 R1.1b 作者状态。具名变体入口/资产的移除与材质格式升级留在后续迁移步骤，未完成前不把 R1.2 整体标记 DONE。

R1.2a 结果：显式 uint32 enum、普通 bool/enum uniform、keyword 静态属性、每 Pass features/稳定键、64 组合预算、固定 GPU 布局、逻辑 Schema 与字段分离。验证：RTX 5060，Debug/Release 定向各 5/5（shader.contracts、双后端 material/render_state）；DXIL/SPIR-V 编译验证 6+2 组合，读回证明静态值选择实际程序且静态独占材质无 uniform 分配，改名/保存/非法枚举迁移/超额诊断通过，Validation 错误为 0。文档：[静态轴](../docs/shader-features.md)。

- [x] enum 稳定数值、普通 bool/enum uniform 与 keyword 特性；每 Pass 显式 features、稳定 VariantKey 和组合预算。
- [ ] 材质序列化/Inspector/热重载统一属性值，替换任意 variant 字符串；状态修改不重编译，静态属性整代切换。

验收：组合数量、枚举互斥、非法值/缺失组合/超额诊断、固定布局与异步发布一致性。

### R1.3 参数接口与显式 HLSL 库

状态：IN_PROGRESS；R1.3a 参数契约/代码生成基础 DONE（2026 年 10 月 26 日）；R1.3b 场景绑定/HLSL 迁移尚未完成。

R1.3a 结果：cbuffer Schema ID/version/type、独立布局 hash、模块别名、HLSL 类型头/绑定头分离、C++ POD/显式打包生成、代际 BindingHandle 与 Schema/资源语义解析。验证：RTX 5060，Debug/Release 定向各 6/6（parameters/shader、双后端 surface/material），生成头实际编译；矩阵/数组/bool/稀疏枚举/范围/NaN、错误版本/代际、模块与资源别名独立性通过，Validation 错误为 0。文档：[参数接口](../docs/shader-parameters.md)。Material/Scene 的准备输入仍使用原适配，完整接入留给 R1.3b，不将基础 handle API 等同于迁移完成。

- [ ] `.params` 接口身份/版本/布局校验、生成 C++ 数据/打包接口及 HLSL 类型、类型化绑定 handle 与标准资源语义。
- [ ] 拆分 View/Object/Lighting/Shadow，入口绑定适配与纯函数库分离；纹理参数和光栅梯度显式传递。
- [ ] 移除 Renderer 的 Frame 逐字段名称匹配、内置 Shader 名称白名单和资源 symbol 分支；绑定仍经过 Graph 访问声明。

验收：项目自定义 Shader 的最小接口、资源/常量别名独立性、DXIL/SPIR-V 函数传参和布局校验、缺少必需接口的准备期诊断。

### R1.4 全量迁移与回归

- [ ] 迁移内置/项目 Shader、示例材质、glTF 语义映射和工具；显式格式升级与缓存失效，不长期维护两套路径。
- [ ] 稳定 ID 不变时重命名 Pass/属性 symbol/资源别名/HLSL 文件，画面与分类保持一致；用户覆盖材质状态不被导入器重写。
- [ ] Debug/Release 双后端回归 M4 PBR/HDR/IBL/阴影/透明、热重载、窗口交互、工具及独立 Player。

退出条件：以上契约实际实现并通过行为验证，记录硬件、配置、命令和限制后才能标记 DONE；文档与语法设计不构成退出条件。

## M5 Deferred 与光追基础

每个子阶段独立验收。先共享数据和有限材质，避免同时开发任意 Shader 命中调度与完整积分器。

### M5.1 Deferred（可选组合）

- [ ] GBuffer 契约、深度重建、Deferred Lighting，透明继续 Forward。
- [ ] 验证同一材质的 Forward/GBuffer/Shadow 角色与固定布局。
- [ ] 比较同一受控场景的 Forward 和 Deferred，解释预期差异。

### M5.2 加速结构与 Ray Query

- [ ] BLAS/TLAS 尺寸查询、构建、scratch 与图访问声明。
- [ ] 静态网格复用 BLAS，Transform 变化更新 TLAS，正确管理在途版本。
- [ ] 用 Ray Query 实现独立 RayTracedShadow 或 AO Feature，共用实例索引，以 ShadowVisibility 等明确资源接入光栅照明。

验收构建后查询、对象增删/移动、相机变化以及不支持能力时的诊断。动态顶点与蒙皮先报告不支持。

### M5.3 Ray Tracing Pipeline

- [ ] RayGen、Miss、通用 Hit Group、ShaderTable 和 TraceRays。
- [ ] 通过 Mesh/Instance/Material 索引访问基础 PBR，不为每个材质资产生成命中程序。
- [ ] 明确显式纹理 LOD、Payload、ShaderTable 对齐和热重载重建规则。

验收在支持的双后端设备上运行同一最小不透明三角形场景；不能用 Ray Query 成功代替此项。

### M5.4 混合效果

- [ ] 选择独立光追反射或 GI Feature，声明 Depth/Normal/Material/AS 输入和 Radiance 输出；按算法由 Forward 表面预通道或 GBuffer 提供输入。
- [ ] GI 的追踪、时域重投影、空间过滤和照明合成按依赖分 Pass，保留无降噪参考，不作为照明内部隐式副作用。
- [ ] 按 ViewID 管理历史与版本，定义相机/场景/材质/尺寸变化的失效规则。
- [ ] 记录支持材质范围与真实 GPU 成本，保留纯光栅对照。

退出条件：Deferred、Ray Query 和 TraceRays 分别有验证入口；至少一个光追效果接入渲染流程。硬件未验证的后端明确记录阻塞，不能把主阶段直接标为全部通过。

## M6 GPU Driven 与执行优化

按 CPU 基线、GPU 提交、并行录制、多队列、Alias 的顺序分别评估。M6.1 与 M6.2 完成可构成最小 GPU Driven 成果，后续优化不要求一定提升所有场景。

### M6.0 Forward+（可选组合）

- [ ] 在普通 Forward 与场景光源数据基础上增加 Tiled/Clustered 光源分类 Compute Feature，声明深度、光源 Buffer、索引表和容量。
- [ ] Opaque/Transparent 显式读取分类结果，保留普通 Forward 基线；处理空光源、溢出、透明覆盖与不同分辨率。
- [ ] 相同场景比较图像、显存和 GPU 成本，不预设一定更快。

### M6.1 可索引资源与间接提交

- [ ] 让已有 GPU Scene 的对象、材质和纹理通过明确索引消费。
- [ ] 实现所需的描述符数组能力和在途安全，不建立无需求的全资源 Bindless 框架。
- [ ] 用 CPU 生成已知 Indirect Args，验证 DrawID/InstanceID/firstInstance 和 Count 映射。

验收结果与直接绘制一致。先隔离间接提交语义，再接入 GPU 剔除。

### M6.2 GPU 剔除

- [ ] Compute Frustum Culling、可见实例压缩、Indirect Args 和 Count。
- [ ] 图中声明清零、写入和 Indirect Read；CPU 只遍历有限 Pipeline 批次。
- [ ] 零物体、全剔除、全部可见、容量溢出有确定处理，禁止本帧同步读回数量。

验收与 CPU 剔除结果对照并测量不同规模场景。Hi-Z 和 LOD 后置，不能混入首个正确性样例。

### M6.3 CPU 并行录制

- [ ] 固化编译后执行计划、线程独立 CommandContext 和分配上下文。
- [ ] 录制只读的冻结访问计划与闭包数据，Shader/PSO 事先准备，绑定分配使用线程安全上下文。
- [ ] 保持单队列，比较串行/并行录制输出和 CPU 时间。

### M6.4 GPU 多队列

- [ ] 首先增加一个适合异步执行的 Compute 工作，再考虑独立 Copy Queue。
- [ ] 队列能力、所有权转移、提交完成点和跨队列等待。
- [ ] 正确消费导入上传资源，跨帧 History 带完整完成信息。

验收竞争资源、只读共享和连续帧情况，Capture 中能解释等待点。记录有无实际重叠和收益，不强制所有 Compute 异步化。

### M6.5 内存 Alias

- [ ] 在已有资源池之上增加 placed/alias 分配、兼容性与 alias 屏障。
- [ ] 基于真实队列 happens-before 关系判断重叠，不只看拓扑位置。
- [ ] 与关闭 Alias 的同一执行器比较画面、内存和时间。

退出条件：各子能力有独立开关和证据；GPU Driven 在双后端验证，调度与 Alias 正确性独立成立。优化没有收益时保留测量和默认策略，不为制造收益增加无效场景限制。

## M7 C# 编辑器与完整项目工作流

先做一条材质编辑工作流，再扩展资源面板。运行时规则继续由 C++ 服务维护。

### M7.1 桥接与视口试验

- [ ] C ABI、Handle 代际、字符串/缓冲区所有权和错误返回。
- [ ] Avalonia 原生视口托管，验证双后端 Present、DPI、resize、最小化和关闭。
- [ ] 输入焦点、捕获与视口内覆盖层，验证 Docking 方案的关键行为。

先完成此集成试验再投入完整 UI。若宿主技术需调整，仅替换编辑器宿主，保持 C++ 服务接口。

### M7.2 材质编辑与事务

- [ ] Schema 自动 Inspector、未保存材质预览、参数验证。
- [ ] Begin/Update/Commit/Cancel 编辑事务，连续拖动形成一条 Undo。
- [ ] UI 快照和命令队列不直接暴露 GPU 对象，编译事件显示错误位置。

### M7.3 项目与资源操作

- [ ] 创建/打开/关闭项目、资源树、搜索、导入进度。
- [ ] Move/Rename/Copy/Delete 调用统一资源服务，处理引用与冲突。
- [ ] Library 重建、损坏缓存恢复、编辑资源与外部变更冲突提示。

### M7.4 场景编辑

- [ ] Hierarchy、选择、Transform、拾取、Gizmo、组件属性与保存。
- [ ] Undo、dirty 状态、关闭未保存文档和重开场景。
- [ ] GPU Scene 随编辑正确更新，历史效果在必要时失效。

### M7.5 运行与构建

- [ ] 独立 Player 和内容处理，按配置收集资源与必需 Shader 变体。
- [ ] 从导出目录独立运行，不依赖原项目 Library 或开发机器路径。
- [ ] 原生崩溃保留诊断；首版不开发进程隔离和 IPC 恢复系统。

退出条件：创建项目、导入模型、从 Shader 创建材质、编辑场景、保存重开、导出运行全流程通过。

## M8 专题深化与作品交付

先选择一个主专题并写清目标、参考基线和必要输入，其他专题保持 DEFERRED，不默认全部实施。

候选为 TAA 与运动矢量、进阶光追反射与降噪、基础路径追踪、Hi-Z/LOD GPU Driven。Path Tracing 使用独立组合：场景/AS → Trace/采样 → History 累积 → 可选降噪 → ToneMapping；替代普通光栅主体，共用资源图和基础材质，不插在 Transparent 阶段。路径追踪先验证采样、BSDF/PDF、累积和收敛，再据测量决定是否建设 Wavefront 队列；降噪前保留原始结果作为正确性入口。

- [ ] 明确选择、支持范围与对照条件，拆成可验收的小步。
- [ ] 建立正确性参考，固定随机序列与相机并处理历史失效。
- [ ] 提供不同场景/规模的 CPU、GPU、内存数据与 Capture。
- [ ] 记录局限、误差和退化情况，避免只展示一个有利场景。
- [ ] 发布可运行演示、构建说明、操作说明、录屏和技术文章。

退出条件：所选专题完成且可复现，作品能由他人运行，讲解能从画面追踪到程序、资源和 GPU 执行。

## 已完成阶段

已完成：M0 工程与设备基础、M1 最小双后端 RHI、M2 Shader 与材质数据链路、M3 单队列 Render Graph 与普通 Forward。摘要保留在对应章节，本节只维护索引。
