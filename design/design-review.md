# RenderLab 设计复查

复查日期为 2026 年 10 月 23 日。本轮从实时与离线渲染系统的公开资料检查总体设计，重点是减少重复状态、隐式依赖和过早建设。结论是保留分层和双后端方向，收紧状态所有权、材质布局与高级功能边界。

这里引用的是公开说明的机制与限制，不能据此断言相关项目存在未修复缺陷。以下取舍是 RenderLab 自己的设计决策；修订已并入 [总体设计](architecture.md)，实现验收进入 [阶段计划](phaseplan.md)。运行正确性与性能结论以阶段计划的实际验收为准。

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

## M4.5 演示宿主与测量

原生调试面板采用同线程 modeless Win32 控件，避免在 C# 编辑器前引入第二套 UI 框架。固定纹理入口沿用显示 Shader 与 Graph 资源声明，允许编译器剔除不再显示的 Forward 工作。Timestamp 以真实提交完成点保护读回，每个原生命令上下文独立复用 query 存储；窗口收集已退休的两帧结果，离线测量单独预热并在关闭验证层后采样。Player 复用同一宿主与项目格式，用独立安装目录检验运行时内容边界，不另建一套渲染流程。

## M4 后 Shader/Material 契约修订提案

用户指出精确名称匹配与共享 HLSL 全局名的耦合。[v2 提案](shader-material-v2.md) 参考 Unity 的固定状态、Surface/Blend 策略和结构体参数求值，采用类型化 Pass role、显式状态及接口绑定 handle。资源 symbol 仅是局部作者名称，稳定 PropertyId/SchemaId 承担数据身份；普通 bool/enum 为 uniform，keyword 特性才生成有界的局部静态轴。透明分类、Blend、DepthWrite 与静态变体分别管理，标准配方提供默认值，显式 Pass 覆盖保持权威。

保留已有固定布局、不可变快照、整代热重载和有界 PSO 生命周期；不复制完整 ShaderLab，不加全局关键字或通用表达式 DSL。模板测试必须配套真实深度模板格式、动态 reference 与 Graph 的各面访问/初始化，不能以语法存在视为实现。当前仅完成提案，运行时与 M4 验收结果未改变，实施和回归要求统一登记在阶段计划。
