# RenderLab 设计复查

复查日期为 2026 年 10 月 23 日。本轮从实时与离线渲染系统的公开资料检查总体设计，重点是减少重复状态、隐式依赖和过早建设。结论是保留分层和双后端方向，收紧状态所有权、材质布局与高级功能边界。

这里引用的是公开说明的机制与限制，不能据此断言相关项目存在未修复缺陷。以下取舍是 RenderLab 自己的设计决策；修订已并入 [总体设计](architecture.md)，实现验收进入 [阶段计划](phaseplan.md)。运行正确性与性能结论以阶段计划的实际验收为准。

## 固定场景渲染工作台设计复查

2026 年 10 月 30 日，用户要求本轮暂不修改资源与场景，以给定固定场景验证 Dock 编辑器、运行配置、Render Graph 与性能观察。详细设计和拟议数据契约见 [工作台](../docs/editor-workbench.md)，实现任务只记录在 M7.2；原材质/项目/场景/导出任务顺延到 M7.3–M7.6，本轮延期。

- 参考 Unity 的工具布局与按帧检查方式。已只读检查用户指定 [Gal.Net dev](https://github.com/StarryXYJ/Gal.Net/tree/dev) 的 `774b35f7561708a785f5fa1109efebe3b9aa5da5`：EditorDockFactory、DockViewLocator、DockLayoutSerializer、UrsaDockHostWindow、MainWindow/App 与包版本。其 Avalonia 12.0.5 / Dock 12.0.0.2 / Ursa 2.1.0 组合是依赖候选依据，不代替 RenderLab 的恢复、构建和原生 HWND 集成验证。只参考具体布局、面板 ID 和持久化组织，不移植完整扩展框架。
- 固定场景需要区分源数据和运行覆盖。光照、导航相机、曝光与管线可以临时变化，配置经原生 owner 在帧边界应用；预设只保存 UserSettings。暂不引入材质 Inspector、场景编辑事务或资源文件操作。
- 图形 API 是 Device 的创建属性。运行中切换采用串行退休/销毁/重建，保留 UI 与可序列化运行状态；历史纹理、PSO、AS 和完成点全部失效。失败时重建原后端也可能失败，应保留 Failed/Retry；这不是设备丢失恢复，也不保证无停顿。
- NativeControlHost 的 airspace 与父窗口生命周期限制仍然成立。Dock 拖放时可能需要隐藏原生子窗；跨父窗口通过重建 Session 完成，不能以托管 Dock 库支持浮窗推断活动 Swapchain 能直接重挂接。该项必须先于复杂面板验收。
- Render Graph Viewer 展示实际 Plan，以访问矩阵和可选择依赖图减少大型全连接图的阅读负担。快照有源帧、Session、配置与拓扑版本；补足诊断字段，默认不增加 Copy/Readback。旧版本和 Alias 逻辑身份都不保证旧像素仍存在，RT 预览需要额外显式保存与完成点。
- CPU API 调用数、间接调用数、几何 batch、候选/可见/提交 instances 是不同统计。GPU 模式的实际可见数不默认读回；计时未完成或未开启必须显示未知。GPU Pass 总和、GPU span、CPU 帧工作和提交完成观测延迟分开，避免把并行区间累加成帧延迟或宣称已测输入到屏幕延迟。
- 当前 C ABI v4 只覆盖部分配置/统计。新增快照和配置采用同版本 DLL/托管程序升版、定宽 POD 与低频 UTF-8 JSON，保持有界队列和不可变值快照；不在 UI 持有 Plan/GPU 指针、不每帧序列化完整图。文档设计不代表这些接口已经实现。
- 用户后续收紧默认布局：仅 Scene 与 Render Settings 可见，Graph/Profiler/Log 通过顶部菜单打开；诊断布局与用户保存布局仍可恢复。托管接入 Serilog 和有界 Log 面板，原生日志以可选队列镜像批量汇入，保留原始文件/来源/时间。面板关闭、清空和筛选不改变日志记录或 GPU 验证错误计数；不把日志洪峰变成逐事件 UI 调度或在 GPU 回调中调用 C#。

## 需要优先修订的设计

| 问题 | 原设计的风险 | 本轮处理 | 验证位置 |
| --- | --- | --- | --- |
| 图和 RHI 状态管理责任含糊 | 两套状态机产生重复屏障或状态冲突 | 图生成同步计划，RHI 显式执行；图外任务通过状态与完成点交接 | M1、M3、M6.4 |
| 延迟回调没有具体内存规则 | Setup 局部引用在 Execute 时失效 | 图拥有 Execute 闭包值，稳定快照，记录声明位置，限制资源解析范围 | M3.1 |
| 材质允许任意变体布局 | 每个 Pass 重打包、热重载和 GPU Driven 分支增多 | 每个 Shader 固定 MaterialLayout，生成 PackingPlan 并反射校验 | M2.2 |
| Template 像独立资产系统 | 元数据、模板与编译包重复缓存和失效 | Template 是 ShaderPackage 内的只读 Schema 视图 | M2.2 |
| Shader/PSO 准备时机不明确 | 在 Execute 中同步编译，妨碍并行且引入卡顿 | 准备阶段处理，发布时检查必需组合，统一 Pipeline 缓存 | M2.3、M2.4 |
| 导入资源和写入版本语义不足 | 过度禁止剔除，或丢弃部分写入所需旧值 | Import 与副作用分开，区分 ReadWrite、部分写入和完整覆盖 | M3.2 |
| 预留 Unsafe 容易成为常规路径 | 隐式全局状态让依赖声明失去意义 | 首版不开放逃生接口；需要外部 SDK 时再定义有边界 ExternalPass | M3、后续 SDK 接入 |
| 光追共享材质的范围过宽 | 假设任意光栅 Shader 能直接用于命中程序 | 有限内置材质、通用 Hit Group、明确采样上下文和支持角色 | M5.2、M5.3 |
| GPU Driven 与图的职责可能混合 | 本帧读回 Count 或为动态工作创建大量 CPU 图节点 | 图组织固定阶段，Shader/间接命令消费动态 GPU 工作 | M6.2、M8 |
| 资源池只描述增长 | 大型导入和 AS 构建后长期保留峰值内存 | 记录占用与高水位，设上限和安全点清理 | M1.2、M5.2 |
| 帧索引和展示索引没有区分 | resize 或不同交换链缓冲数下复用在途内存 | FrameContext 按提交完成复用，与图像索引分离 | M1.1 |
| 只保护已提交 GPU 引用 | 已录制未提交的命令仍可能引用被释放对象 | 录制上下文保活资源，提交转移给完成点回收，取消释放 | M1.1 |
| 高级阶段过大 | 光追、间接提交、多线程和多队列同时出错难定位 | 拆成独立子阶段、开关、基线和退出条件 | M5、M6 |

## 间接实例索引的实测边界

2026 年 10 月 29 日，M6 的索引隔离测试发现：D3D12 SV_InstanceID 从每条 Draw 的 0 开始，DXC 默认 SPIR-V SV_InstanceID 映射为含 firstInstance 的 InstanceIndex。仅比较每个后端的直接/间接画面会同时接受两种不同对象身份，因而必须另以固定对象颜色检查预期实例集合。

公共命令保持原生 16/20 字节 ABI，不为注入 Root Constant 扩大参数格式，也不依赖不可统一的 DrawID。Shader helper 在 DXIL 加上 SM 6.8 SV_StartInstanceLocation，在 SPIR-V 消费默认绝对实例 ID。DX12 索引 Shader 路径显式要求 SM 6.8，普通基础 Shader 继续使用 SM 6.0；能力不足时报错，不把本帧参数读回 CPU 补齐。两后端 RGBA8 常量插值出现 1 LSB 的舍入差异，验证以同后端逐字节一致和独立实例 ID 检查为准。

## 实时引擎的参考依据

### Unreal RDG

RDG 明确区分 Setup 和延迟执行；文档指出回调内存生命周期、外部资源延长生命周期，以及立即模式与并行执行的调试差异。[官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)

RenderLab 保留数据拥有权、访问验证和源位置诊断。首期用同一执行器的串行/禁用优化开关排错，暂不复制其完整参数宏、任务系统和立即模式。

### NVRHI

NVRHI 说明并行录制和提交顺序使录制时的跨命令列表状态推断不可行，并描述上传和 scratch 管理器保留高水位的行为。[编程指南](https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md)

RenderLab 不同时引入图编译器与隐式 RHI 自动屏障。独立 RHI 样例使用显式状态；内存池有观测和回收入口。采用已有显存分配器，但资源何时复用仍由引擎负责。

### Filament

Filament 的 FrameGraph 以资源和 Pass 表达依赖；材质工具提供变体过滤，文档同时提醒被裁掉的变体若在运行时需要会出问题。[FrameGraph](https://google.github.io/filament/notes/framegraph.html)、[材质指南](https://google.github.io/filament/main/materials.html)

RenderLab 从简单图结构起步，首版保守跟踪 Buffer。构建产物带必需变体清单，变体裁剪可验证；不因为支持双后端就对所有 API 能力建立组合开关。

### Unity

Unity 的 Unsafe Pass 用于调用兼容性 API，优化文档指出此类路径会限制图优化。[Unsafe Pass](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/render-graph-unsafe-pass.html)、[图优化](https://docs.unity3d.com/cn/6000.0/Manual/urp/render-graph-optimize.html)

RenderLab 学习属性声明与资源访问方式，首版不建设兼容模式、全局 Shader 参数通道或任意命令逃生接口。程序绑定、Pass 资源和外部副作用都要可见。

## 离线渲染的参考依据

pbrt 第四版讨论 GPU 上的寄存器、分支与内存组织，以及 Wavefront 的收益和成本；其工作队列章节说明本帧把 GPU 队列数量读回 CPU 会引入同步。[GPU 映射](https://pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU)、[工作队列](https://pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Implementation_Foundations)

RenderLab 共用场景身份和表面求值，但积分器负责 BSDF 求值、采样和 PDF，Render Graph 只负责可静态组织的阶段。简单路径追踪先给出正确参考，再用数据判断是否改为 Wavefront；不提前复制离线系统的全部材料、介质和光谱框架。

GPU Driven 和路径队列都保持动态数量在 GPU 上消费。射线 Payload 与队列元素保持紧凑，避免把光栅所需的完整中间对象照搬进光追热路径。

## 主动后置的复杂度

- 不新增独立 MaterialTemplate 编辑器、资产类别或缓存。
- 不建立两套可互相补救的资源状态追踪器。
- 不为首期一个设备和一个视口建设分布式或跨进程协议。
- 不为每种 RenderFeature 建设插件注册体系；先使用具体函数和有类型的数据。
- 不做图执行计划缓存、精细 Buffer 区间调度和通用表达式 DSL。
- 不实现自动设备丢失恢复、通用流送或任意 C++ 代码热更新。
- 不把所有材质资产变成光追 ShaderTable 中独立命中程序。
- 不把 GPU Driven、多线程录制、Async Compute 和内存 Alias 作为一次性交付。

后置内容只在真实用例和测量结果支持时进入相应阶段。保留 CPU 直接绘制与单队列执行用于正确性和性能对照，这两条基线具有实际用途。

## 实现前仍需验证的事项

| 事项 | 最早验证点 | 结果影响 |
| --- | --- | --- |
| 实际 GPU、驱动和 SDK 能力 | M0.3 | 光追、描述符索引和多队列的可验证范围 |
| DXC 目标与生成布局规则 | M1.3、M2.2 | PackingPlan、绑定映射和固定布局方案 |
| 资源池、Alias 与调度的收益 | M3、M6 | 默认策略和是否值得开启 |
| C# 原生视口与 Docking 集成 | M7.1 | 编辑器宿主方案，不影响渲染核心 |

本轮没有发现必须取消既定方向的架构冲突。上述事项保留为有退出条件的实现验证，不以增加泛化框架提前解决。

## 2026 年 10 月 24 日：Graph API 与普通 Forward

- 名称只用于单图建图，执行使用类型化版本 handle；不增加全局资源管理器。
- 一个 Builder 声明访问，一个 Execute 保存闭包；移除公开 PassData/PrepareContext 与多段调用，内部复用原编译器、资源池和 RHI 命令包。
- Raster/Compute/Copy Context 表达不同命令；Material、场景与绘制策略留在 Renderer。
- Opaque/Transparent 独立 Feature；最初的 ForwardPipeline 组合器已在下述实例化修订中统一为 RenderPipeline。硬顺序不代替内容依赖，中间 Feature 剔除后前后存活阶段仍有顺序。
- 阶段快照真实复制；旧版本 handle 和别名不能替代独立图像。
- Demo 无场景，直接组图。普通 Forward 接收已准备 DrawPacket；Forward+、Deferred、光追和 Path Tracing 仅维护后续计划。

## Feature 实例与 Create 依赖解析

用户确认 Feature 基类和流式注册，并要求注册完成后 Create、注册顺序无关。采用稳定 Feature 名称及显式依赖，在 Create 进行一次拓扑排序和阶段冲突诊断；依赖不从执行回调或尚不存在的图资源猜测。Create 无 GPU 操作，冻结组合，后续每帧 Build 才解析名称、声明 Pass。Custom/Forward 只影响初始注入，使用同一个组合器。移除旧函数式 Feature 和闭包注册入口，避免双轨 API。

## M4.1–M4.2 最小场景闭环

采用值类型场景与静态 glTF 导入，UUID/子资源映射保存于 .meta，Library 只作可重建缓存。Blinn–Phong 先验证矩阵、法线、颜色、材质和 Forward，PBR/HDR/阴影后置。方向光作为场景组件，可有多个；按稳定 UUID 选一个，写入共享帧常量，Lighting 库读取，不增加 CPU 单例或 LightManager。项目 Shader 复用 Core/Lighting 与 .params 生成，Material 返回绑定值以消除与 Graph 的 BindingGroup 准备重复。导入子集和身份键限制见 M4 文档。

## M4.3 Shader 材质与 HDR/IBL

Lit/Unlit 保持普通 Shader 资产，表面值、光栅 SampleGrad 上下文、BRDF 与照明分开。HDR 使用 RGBA32F 正确性基线，显示变换显式进入 Graph；外部图资源通过明确 deferredResources 延后绑定，不以占位纹理伪装实际依赖。IBL 先采用确定性 CPU 预滤波 2D 图集和 BRDF LUT，遵守当前单 mip/layer 的 RHI 边界；明确低分辨率、LDR 环境源、导数切线框架和 split-sum 近似的局限，避免同时扩张 cubemap/mip、流送和离线烘焙系统。数据与采样契约见 [M4.3](../docs/m4-forward-pbr.md)。

## M4.4 单张阴影与 Alpha 角色

先用颜色记录深度的单张正交阴影验证完整遮挡链路，D32 保持只用于附件，避免同时扩张深度采样与无颜色 PSO。RGBA32F 的存储代价显式记录；手动 3×3 PCF 采用接收平面深度修正。Caster 候选与相机可见集分开，防止离屏投影丢失。Masked/Transparent 作为 Lit/Unlit 普通 variant，沿用固定布局与 Forward 的 Opaque/Transparent 阶段；透明不投影且不解决相交排序。Sky、DepthPrepass、Bloom 和级联均等实际用例再增加。

## M5.1 Deferred

M4 的单颜色、不可采样深度约束已不足以表达 Deferred。RHI 与 Render Graph 现支持最多八个同时写入的颜色附件，并通过独立采样视图读取 D32/D32S8。首个 GBuffer 固定使用三张 RGBA32F：BaseColor/Metallic、世界法线/Roughness、Emission/Occlusion；Reversed-Z 深度为零的像素不参与光照。Lit 的 Forward/GBuffer 共用表面求值，透明材质仍走 Forward，缺少 GBuffer 角色的自定义不透明 Shader 明确报错。三张全分辨率目标成本较高，先作为正确性基线；压缩布局和 Forward+ 留到实际测量后再规划。

## M4.5 演示宿主与测量

原生调试面板采用同线程 modeless Win32 控件，避免在 C# 编辑器前引入第二套 UI 框架。固定纹理入口沿用显示 Shader 与 Graph 资源声明，允许编译器剔除不再显示的 Forward 工作。Timestamp 以真实提交完成点保护读回，每个原生命令上下文独立复用 query 存储；窗口收集已退休的两帧结果，离线测量单独预热并在关闭验证层后采样。Player 复用同一宿主与项目格式，用独立安装目录检验运行时内容边界，不另建一套渲染流程。

## M4 后 Shader/Material 契约修订提案

用户指出精确名称匹配与共享 HLSL 全局名的耦合。[v2 提案](shader-material-v2.md) 参考 Unity 的固定状态、Surface/Blend 策略和结构体参数求值，采用类型化 Pass role、显式状态及接口绑定 handle。资源 symbol 仅是局部作者名称，稳定 PropertyId/SchemaId 承担数据身份；普通 bool/enum 为 uniform，keyword 特性才生成有界的局部静态轴。透明分类、Blend、DepthWrite 与静态变体分别管理，标准配方提供默认值，显式 Pass 覆盖保持权威。

保留已有固定布局、不可变快照、整代热重载和有界 PSO 生命周期；不复制完整 ShaderLab，不加全局关键字或通用表达式 DSL。模板测试必须配套真实深度模板格式、动态 reference 与 Graph 的各面访问/初始化，不能以语法存在视为实现。实施和回归要求统一登记在阶段计划。

实施复查将 R1.1 分为 RHI/Graph 和作者层两步。附件格式与状态正交；无深度测试但需写入时统一映射 Always。Graph 暂不拆模板平面的版本与布局，采用整附件保守依赖和逐面初始化/权限检查，任一 Load 都保留旧 producer，避免清深度时丢失模板。该基础独立验证后，再接入材质属性引用，减少临时兼容接口。

收尾复查确认每类身份各有单一来源：资产 UUID 管引用，PropertyId 管保存，Schema ID/version/hash 管参数兼容性，包/Pass handle 管当前代绑定；显示名、路径和 HLSL symbol 仍可局部使用，不承担宿主行为策略。glTF 通过类型化 MaterialSemantic 映射，外部 IBL/阴影另用 BindingSemantic；两者不会形成全局可变参数表。静态值和普通值共用材质属性存储，旧具名变体与材质 v1 明确拒绝，避免两套选择状态长期并存。独立无 sidecar 工具允许路径派生的临时身份，持久资产要求登记 sidecar；限制和显式迁移步骤见 [资产契约](../docs/shader-assets.md)。

## B3 资源复用

B2 测量将差距定位到逐帧原生分配/回收，优先按生命周期复用，保持渲染算法和单队列基线。帧页按 CPU 引用与完成点回收，材质保存不可变版本；描述符分配由逻辑组保活，缓存页不持有场景资源。Profiling 的 CPU 读取期独立于 GPU 完成，默认 View 使用弱回指并由物理所有者持有，交换链 resize 明确清理 View。避免整 Prepared 缓存钉住帧资源，也不通过移动析构计时或改变 benchmark 等待策略取得表面收益。[设计](resource-reuse.md)、[接口](../docs/resource-reuse.md) 与 [配对结果](../docs/performance/b3-resource-reuse.md) 分开维护；阶段验收仍以计划为准。

## B4 基础计算复用

2026 年 10 月 30 日按用户授权先建立同实现基线，再减少重复工作。Scene 文档可直接修改，故首版
Extractor 比较对象值和实际 world/localBounds，不把裸指针、模型 key 或未建立的 dirty 版本当唯一依据。
缓存只在提取成功后发布，previousWorld 保持上次成功提取语义；相机移动仍解析全层级，未引入 ECS
或局部层级调度。完整快照和 GPU Scene 投影继续沿既有边界生成。

Bounds 没直接使用浮点 center/extents 公式，而是按轴选支持角，保留既有 Point 的求和顺序，
避免仅因重新结合运算改变边界上的保守结果；与八角标量参考做随机及 shear 对照。
TRS 合法性保留原 pivot 门槛，只去除无用的逆矩阵构造。视锥平面批处理留给独立测量/契约，
不在缓存阶段同时改变 CPU/GPU 可见集语义。实测和局限见 [B4 记录](../docs/performance/b4-foundation.md)。

数学库采用 Windows x64 范围内的 DirectXMath 3.21，边界保留 POD 存储与 Pack。
普通矩阵乘法保持 A×B，四元数按库 Q2×Q1 的定义适配；对结果使用双精度参考误差界而非强行保留标量结合顺序。
通用 Inverse 尚未迁移：库返回 Inf 的接口不等价于项目原有 finite/pivot 异常规则，单一 determinant
门槛也不能替代原规则。静态实例的逆转置已缓存，故不为统一库名称而在本阶段额外改写求逆契约。
它不是对库数值可靠性的否定；后续出现实际热点时可独立评估适配与参考测试。

## M5 相机重投影时间降噪

用户授权在空间滤波基础上尝试时间重投影。静态场景以深度重建世界位置并使用前帧矩阵，不在此步同时扩展对象运动矢量、反射虚拟位置或全套 SVGF/NRD。主实例/材质/照明变化统一失效；对相机运动用世界位置/法线/粗糙度逐 tap 验证、邻域裁剪和视角置信度，历史长度有界。原始 1/N 历史保持独立，时间结果只供显示，高 spp 回到参考，避免把滤波偏差当作收敛修复。

未滤波采样序列在相同场景的相机运动中继续前进，保持 Raw 固定样本零；计数和序列分别管理。时间引导仅为当前活动 View 保留两套三张浮点纹理，降低多 View 同时持有历史的峰值。此布局先验证正确性，尚未压缩；对光滑反射仍可能有视角相关拖影和裁剪偏差。实现契约见 [时间反射](../docs/temporal-reflections.md)，实际验收统一记录在阶段计划。

## 运动反射白点闪烁设计复查

日期：2026 年 10 月 27 日。用户在相机移动、开启降噪时仍观察到强光与反射高光白点闪烁。以下是源码对照和改进建议，不是已实现或经新 GPU 实验确认的根因排序；实施状态只登记于 [阶段计划](phaseplan.md)。

### 外部依据

| 来源 | 可借鉴机制 | 当前项目对应缺口 |
| --- | --- | --- |
| [AMD FidelityFX Reflection Denoiser](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/#amd-fidelityfx-reflection-denoiser) 与 [官方重投影源码](https://github.com/GPUOpen-Effects/FidelityFX-Denoiser/blob/master/ffx-reflection-dnsr/ffx_denoiser_reflections_reproject.h) | 分别估计接收表面与反射虚拟位置的历史坐标，用邻域和几何检验选择；重投影阶段估计方差，再做空间与时间滤波 | 当前只跟踪接收表面，缺少反射视差路径；空间滤波没有方差输入 |
| [SVGF 原始论文与代码入口](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/) | 时间累积亮度统计，利用方差驱动多尺度 edge-aware wavelet 滤波 | 当前仅单次固定 5×5 空间滤波；3×3 当前帧 RGB 统计不等于时间矩历史 |
| [NRD 输入与最佳实践](https://github.com/NVIDIA-RTX/NRD/blob/master/README.md#noisy-inputs) | 明确区分噪声信号与几何引导，使用合适的 specular hit distance、材质去调制和历史修复 | 当前引导直接来自当帧随机 GGX 射线，信号已带主表面材质响应 |

这里的 AMD 源码用于理解算法，未引入第三方代码。集成库或移植实现时另行固定版本、保留许可证，并核对当前 SDK 的输入约定。NRD 可经原生 API 映射到 DX12/Vulkan；实际接入仍需要资源/格式/绑定与 Graph 生命周期适配，不能视作替换一个 Shader 即完成。

### 现有实现的具体风险

1. `ReflectionTemporal.hlsl` 将接收表面世界位置投影到前帧。移动相机时，反射图案未必跟随该表面运动，因而可能复用错误内容或靠裁剪频繁丢弃历史。不能直接把随机次级命中点当作反射画面的稳定运动矢量；AMD 的路径是基于反射距离的虚拟位置/视差近似，并带有效性选择，曲面仍需额外处理。
2. `ReflectionDenoise.hlsl` 和 `ReflectionTemporal.hlsl` 都使用当帧随机命中距离。它不是稳定次级几何；在相同表面上，距离和 hit/miss 也会随采样变化。尤其静止累积后的多样本 radiance 仍只配一个当帧距离，两者代表的统计范围不同。这可能让空间权重和时间支持随噪声变化，需用逐像素诊断确认程度。
3. 时间权重是 `count/(count+1) * strength * support * viewConfidence`。多个置信度直接相乘，可能使有效历史权重远低于 alpha 中的长度。例如 count=16、support=.5、其他因子为一时，历史权重仅约 .47；这是公式示例，不是实测接受率。界面来源帧数也不能表示像素已稳定累积。
4. 当前先对逐帧累积结果做固定半径空间滤波，再做单套时间历史；缺少时间矩、短历史修复和方差引导的多尺度重建。裁剪历史到当前含噪 RGB 邻域主要限制旧内容，无法独立抑制新帧的异常亮样本。
5. `RayReflection.hlsl` 在次级命中处计算直接太阳光 BRDF。狭窄且高能的次级高光可以产生高方差；主反射 VNDF 的 throughput 有界也不能保证次级 radiance 平稳。目前不能仅凭用户观感断言存在焦散、PDF 错误或积分器不收敛。

### 建议改造顺序

以 AMD 的反射专用重投影/空间/时间组织为主参考，并借鉴 SVGF 的亮度矩与方差引导。先测量历史拒绝与亮度尾部，再分步比较稳定命中引导、双重投影、短历史重建和空间滤波，避免同时改变全部变量而失去归因。

建议的显示分支为：`单帧反射 -> 材质去调制/稳定命中引导 -> 表面与反射视差重投影、时间统计 -> 短历史重建/方差引导空间滤波 -> 时间稳定与异常亮点抑制 -> 材质恢复 -> Combine`。这是待实现候选，具体反馈点和通道布局要随实际算法确定；不能把已经长时间平均的 progressive radiance 再当作独立单帧样本累计时间矩。未滤波 progressive 1/N 分支继续独立作为参考，现有高 spp 旁路契约若变更必须显式更新。

异常亮点抑制只作用于显示分支，并需保护真实小面积高光，记录能量偏差。NRD 官方也要求输入在有限历史窗口内合理收敛，anti-firefly 无法挽救任意稀疏高能信号。若输入尾部仍主导误差，再评估采样或照明模型；直接增大历史权重会增加错误反射跟踪的拖影风险。

验收需要固定运动序列的逐帧结果，包含平移、转动、显露遮挡后表面、停止后恢复和强高光。对齐对应帧参考后比较误差随时间的变化，区分真实高光运动与噪声闪烁；同时检查尾随残影、细节、亮度损失、内存与 GPU 成本。已有终点 2048 spp MSE 仍可作为一项指标，但不能代替整段时间稳定性。此次没有修改 Shader、构建或运行新的测试。

### 实施取舍

用户随后授权实施。采用独立单帧 Prepare、双候选重投影/时间矩、三尺度方差引导滤波和材质 Resolve；原始 progressive 分支与时间统计分开。稳定 hit distance 只用于反射跟踪，不直接惩罚接收表面历史权重。有效 bilinear 支持归一化后才根据支持度修正历史长度，避免把多项置信度反复乘入权重；像素诊断同时输出权重、长度、路径和稳定距离。

先保存旧版同轨迹逐帧基线，再做候选对照。邻域中位数/上四分位数构造的输入亮度上限虽然进一步压低闪烁，却损失真实次级高光能量；撤去该截断后，图像误差更低而时间稳定性仍改善。因此不保留硬输入亮度上限，异常高能样本由有限时间复用和方差引导空间滤波平滑。显示历史裁剪仍有偏差，不能当成无偏积分器或完整 NRD。

空间结果不回写时间 radiance/moments，避免递归扩大模糊；短历史用空间方差启动多尺度重建，fast luminance 用于滤波尺度。此步未增加第二套完整颜色历史或独立的后置时间反馈，未接入 SDK、动态对象运动或 TAA。长期布局和有效性条件见 [接口](../docs/temporal-reflections.md)，实际运行结果与被拒绝方案的数据见 [稳定性记录](../docs/performance/m5-reflection-stability.md)。

1440p 窗口初测暴露出为每个滤波阶段分配独立目标的显存成本。Prepare/三尺度/Resolve 改用两张显式交替的逻辑纹理；每次覆盖生成新版本，旧版本的所有读取都在覆盖前结束，由既有 Graph RAW/WAR/WAW 依赖保护。这不引入通用堆内存 Alias，也不把旧版本别名当作需要保存的图像快照；显示别名仅指向最后 Resolve 的版本。

### 静止显示恢复

用户随后反馈约 1800 spp 才达到理想结果。旧 Resolve 以 `1-spp/2048` 混合有限时间滤波与 progressive，在 256 spp 仍有 87.5% 的有限历史分量。该时间历史最多 32 帧，并有当前邻域裁剪及空间偏差；静止 1/N 均值虽在改善，显示却迟迟没有使用它。强光 RayTracing 对未滤波 8192 spp 的初始对照确认：256 spp 未滤波 RGB MSE 约 1.57，旧 Resolve 约 3.26。

单独增加淡出分母虽然减少高 spp 偏差，却在低 spp 重新显露白点。选择在 Resolve 内用稳定滤波亮度作为空间权重引导，对 progressive 均值做独立 5×5 去调制重建；在静止 8–64 spp 平滑交接能量来源。时间亮度只引导邻域权重，不提供静止重建的 radiance；不把 progressive 均值送入时间矩或递归历史。保留原 2048 spp 精确旁路、镜面/背景/Unlit、运动时完整显示路径，复用原资源和绑定。空间权重仍可能受有限时间历史偏差影响，反射边界与小高光可能模糊；没有提高射线样本率、改变 PDF、截断亮度或承诺任意场景在固定 spp 无噪声。实际误差、过渡和成本见 [静止显示记录](../docs/performance/m5-stationary-reflections.md)。

### 光追资源与场景准备优化评估

后续光追管线优化优先保持数值结果：静态几何按不可变导入身份缓存，变更表上传合批；只使用一个 float 的命中引导改为 R32F，未滤波路径省去引导，2048 spp 后省去已经严格旁路的显示重建。非活动时间引导释放 CPU 引用，progressive View 使用软字节预算与 LRU，命令保活继续承担在途回收。HDR 二阶矩和世界位置不直接降为半精度，以免范围/精度误差被误当成采样或重投影改进。

射线偏移改造与纹理 footprint 本轮评估后保留为后续独立工作：当前主 GBuffer 只有着色法线，稳健偏移应先补几何法线/误差契约并覆盖尺度、薄片和大坐标；Atlas 与 RHI 尚无原生 mip 链，ray cone 需先定义 mip/采样 footprint。仅调整固定 epsilon 或把 LOD 0 换成经验常量不能证明这两项已解决。它们不属于本轮无损资源/准备优化的完成项，状态只在阶段计划维护。

### 局部误差与重建足迹

用户反馈亮点先扩散再缩小。固定半径后置核在 8–64 spp 增强，即便静态 MSE 降低，也会出现随时间变化的亮斑。修改为同一条三尺度重建链：从原始单帧维护匹配 progressive 计数的中心矩，以局部均值误差估计连续控制时间先验与空间尺度；大尺度优先随置信度提高而退出，Resolve 不再增加后置空间核。

不能直接把最多 32 帧、来自预滤波信号的时间矩当作数百 spp 的逐像素误差。采用 Welford 中心矩的独立 sidecar，前帧均值从匹配的未滤波 RGB 恢复，每个双缓冲槽仅存中心矩和控制值两个 float。结构化缓冲相比额外 RGBA32F 纹理节省一半空间；沿用 progressive View 缓存、失效与提交所有权，关显示滤波也继续更新。中心矩不滤波；控制目标借用同表面邻域矩，避免尚未遇到高能样本的安静像素过早自信。控制值限制逐帧变化，大尺度不因单个新样本突然扩大，小尺度允许及时抑制高方差尖峰。中间 alpha 传播近似方差，保留相关样本的经验下限；该下限和 Sobol 序列下的误差指标不是严格概率界。

最初仅按像素中心矩降低全部尺度的候选留下更多细白点；同表面稳定误差、保留充分的小尺度重建并传播空间方差后改善。保持稀有输入能量和独立原始均值，不新增输入亮度截断。以逐帧正误差区域、连通亮斑、峰值、亮度偏差、静态误差和运动恢复一起评估；不再仅凭几个 spp 截图认定观感改善。实现和边界见 [接口](../docs/temporal-reflections.md)，实际对照见 [自适应重建记录](../docs/performance/m5-adaptive-reflections.md)。
