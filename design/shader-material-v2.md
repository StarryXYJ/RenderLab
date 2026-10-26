# Shader 与材质契约修订

本设计的作者状态、静态轴、参数接口和资产身份已接入同一运行时；实施状态与真实验收只维护在 [phaseplan.md](phaseplan.md)。当前作者/API 细节见 [渲染状态](../docs/render-states.md)、[静态轴](../docs/shader-features.md)、[参数接口](../docs/shader-parameters.md)、[资产与迁移](../docs/shader-assets.md)。下文函数签名用于说明职责，实际 HLSL 参数以库源码为准。

## 问题与边界

修订前 SceneRenderer 用 `variant == "Transparent"` 分类，按 `"Forward"` / `"Shadow"` 查 Pass，以 `EnvironmentAtlas`、`BrdfLut`、`MainLightShadow` 等 HLSL symbol 补绑定，并逐字段检查整个 `Frame` 布局。Core/Lighting 等库直接读取全局 `Frame`、`Draw`、`Material` 和纹理。改名或拆分文件可能改变运行行为，项目 Shader 也被迫模仿内置 Shader 的内部结构。

修订目标是把程序用途、材质策略、固定功能状态、静态功能选择和 GPU 参数接口分别声明。保留 `.shader` 唯一作者入口、普通 HLSL 库、稳定属性 ID、固定布局、异步热重载和不可变 GPU 快照。不引入完整 ShaderLab、通用表达式语言、全局关键字系统或新的材质模板资产。

名称仍用于源码引用、局部符号解析和反射校验；运行时行为通过已验证的类型与 handle 决定。不能把所有字符串比较都视作问题，也不能仅把任意字符串换成另一套全局字符串表。

## 五类独立契约

| 契约 | 声明与职责 | 运行时消费方 |
| --- | --- | --- |
| Shader 身份 | 资源 ID；Shader 名称用于显示与诊断 | 资源解析、包加载 |
| Pass 用途 | `Forward`、`ShadowCaster`、`Custom` 等类型化 role；Pass 名称自由 | Renderer 选择 PassHandle |
| 材质策略 | SurfaceType、AlphaClip、BlendMode、CullMode、CastShadows、queueOffset | 分类、排序、标准状态配方 |
| 固定功能状态 | Raster、Depth、Blend、Stencil 的完整声明 | RHI PSO 与动态命令 |
| 静态功能 | 特性标记的 bool/enum，以及各 Pass 消费的轴 | VariantKey、程序选择 |

role 是有限的管线契约，首期只实现已有角色；GBuffer、DepthOnly 等随对应功能增加，不提前声称支持。同一 Shader 的同 role 多个 Pass 必须显式选择或报歧义，不能默认取第一个。`Custom` 由调用方显式选择 PassHandle。缺少必需 role 在准备时报告；没有 ShadowCaster 的材质可以不投影。

内置与项目 Shader 均通过资源身份解析，移除 Renderer 对 `RenderLab/Lit` 等名称的行为白名单。项目自定义 Shader 只需实现所选 role 的接口，不必复制内置 Frame。

## 材质策略与状态

`SurfaceType = Opaque | Transparent` 决定常规分类；`AlphaClip` 独立，Masked 表示 Opaque + AlphaClip。透明表面也可以执行 AlphaClip。`BlendMode = Alpha | Premultiplied | Additive | Multiply` 是标准配方的输入，不自动成为编译轴。

标准队列值为 Geometry 2000、AlphaTest 2450、Transparent 3000。queueOffset 限于对应分类范围；先按队列排序，再在同队列采用不透明前到后、透明后到前排序和稳定次键。跨队列的模板遮罩依赖必须保留；需要严格跨 Draw 顺序的自定义效果通过显式阶段或有序批次组织，不能靠 PSO 排序碰运气。

标准 Forward 状态如下，深度测试默认开启，Reversed-Z 使用 GreaterEqual、清除深度为 0：

| 配方 | RGB Src / Dst | Alpha Src / Dst | 深度写入 | 输出约定 |
| --- | --- | --- | --- | --- |
| Opaque | Blend 禁用 | Blend 禁用 | 开 | 线性表面颜色 |
| Transparent / Alpha | SrcAlpha / OneMinusSrcAlpha | One / OneMinusSrcAlpha | 关 | 直通 RGB、覆盖率 Alpha |
| Transparent / Premultiplied | One / OneMinusSrcAlpha | One / OneMinusSrcAlpha | 关 | RGB 已乘 Alpha |
| Transparent / Additive | SrcAlpha / One | Zero / One | 关 | 保留目标 Alpha |
| Transparent / Multiply | DstColor / Zero | Zero / One | 关 | RGB 输出 lerp(1, color, alpha) |

表内 BlendOp 均为 Add。Shader 入口的显式输出编码函数消费 Surface/BlendMode，确保颜色约定与状态配方一致；改变配方不会自动修改任意自定义 HLSL 的输出。

状态模型包含：

- Raster：CullMode None/Front/Back、FrontFace、颜色写掩码。
- Depth：testEnabled、CompareOp、writeEnabled，测试和写入分别声明。
- Blend：enabled、独立 RGB/Alpha 的 SrcFactor、DstFactor、BlendOp。
- Stencil：enabled、readMask/writeMask、front/back 的 CompareOp、Fail/DepthFail/PassOp，以及 reference。

CompareOp 使用 Never/Less/Equal/LessEqual/Greater/NotEqual/GreaterEqual/Always；StencilOp 覆盖 Keep/Zero/Replace/IncrementClamp/DecrementClamp/Invert/IncrementWrap/DecrementWrap。BlendFactor/Op 先覆盖双后端基础集合，双源混合与逻辑操作不在首期范围，不能静默降级。

复查补充：Pipeline 的可选 depthStencilFormat 独立于 Depth/Stencil 状态，关闭测试仍可绑定附件。Depth.test=false 且 write=true 映射为原生测试开启、compare=Always，保证 DX12/Vulkan 都确实写入；test/write 均关才关闭原生深度阶段。动态 StencilRef 由绘制命令传递，不属于不可变 PSO。

解析顺序固定为：状态类型默认值 → 显式选择的标准 preset → 当前 Pass 的字段覆盖。覆盖可以是字面值或类型匹配的材质属性引用；不支持任意表达式。Renderer 消费最终状态，不根据 `blend=true` 推断 depthWrite，也不强制所有 Transparent 都禁止深度写入。Graph 按实际状态验证附件访问。

标准 ShadowCaster 的 enabled 策略为 Opaque 且 CastShadows；自定义投影可显式选择不同策略。透明默认不投影是配方行为，不是 Renderer 根据变体名称作出的禁令。

glTF 导入把 alphaMode、doubleSided 等转换为生成材质的属性默认值；用户覆盖材质具有最终权威，不能在 Draw 准备时重新覆盖 Cull。镜像变换导致的绕序翻转由明确的几何 FrontFace 修正处理。

## 作者入口

```text
shader "Project/Coated"
{
    use "RenderLab/View.params";
    use "RenderLab/Object.params";
    use "RenderLab/Scene.params";
    enum NormalMode { Off = 0; Tangent = 1; Detail = 2; }
    material
    {
        [id("surface")] SurfaceType Surface = Opaque;
        [id("blend")] BlendMode Blend = Alpha;
        [id("cull")] CullMode Cull = Back;
        [id("cast_shadows")] bool CastShadows = true;
        [id("alpha_clip"), keyword("RENDERLAB_ALPHA_CLIP")] bool AlphaClip = false;
        [id("normal_mode"), keyword("RENDERLAB_NORMAL_MODE")] NormalMode Normal = Tangent;
        [id("alpha_cutoff"), range(0, 1)] float AlphaCutoff = 0.5;
        [id("base_color"), color] float4 Tint = (1, 1, 1, 1);
    }
    render
    {
        surface = $Surface;
        blend = $Blend;
        alphaClip = $AlphaClip;
        queueOffset = 0;
    }
    pass "Beauty"
    {
        role = Forward;
        features = { AlphaClip, Normal };
        state { preset = SurfaceForward; cull = $Cull; }
        vertex = "Coated.hlsl:VSMain";
        pixel = "Coated.hlsl:PSMain";
    }
    pass "SunDepth"
    {
        role = ShadowCaster;
        enabled = OpaqueShadow;
        castShadows = $CastShadows;
        features = { AlphaClip };
        state { preset = ShadowCaster; cull = $Cull; }
        vertex = "CoatedShadow.hlsl:VSMain";
        pixel = "CoatedShadow.hlsl:PSMain";
    }
}
```

`SurfaceForward` / `ShadowCaster` 是固定、类型化的内置状态配方，`OpaqueShadow` 是读取 render.surface 与 castShadows 的固定 enabled 策略。它们不是可任意调用的脚本。`$` 引用在编译期解析为 PropertyId/类型；Shader 作者可以更改属性 symbol、Pass 名称、文件名而不改变行为。纯自定义 Pass 可以使用显式队列分类、enabled 常量和完整 state，不依赖 Surface 配方。

固定状态也可以直接写成以下形式，Stencil 的公共字段默认同时作用于 front/back，可用独立 face 块覆盖：

```text
state
{
    depth { test = true; compare = GreaterEqual; write = false; }
    blend { enabled = true; srcColor = SrcAlpha; dstColor = OneMinusSrcAlpha; }
    stencil
    {
        enabled = true;
        reference = $StencilRef;
        readMask = 255; writeMask = 0;
        compare = Equal; fail = Keep; depthFail = Keep; pass = Keep;
    }
}
```

`StencilRef` 需声明为受范围约束的 uint 属性。附件 Load/Clear/Store 和 clearStencil 属于 Graph，不放到材质里。状态只控制已绑定附件的测试和写入。

## bool/enum 特性与变体

普通 bool 是运行时 uint32 参数，普通 enum 是具有显式稳定数值的类型化 uint32 参数，Schema 为后续 Inspector 输出类型/选项信息；当前没有材质编辑器 UI。`color`、`range` 等编辑特性与编译特性分别处理。只有 `[keyword(...)]` 才声明材质局部静态轴，仅允许 bool/enum；float 等无限取值类型不支持。

- bool 生成 0/1 宏；enum 生成一个数值选择宏和具名枚举常量，不生成可同时启用的若干独立 bool。
- 属性值只按 PropertyId 存储一次。静态值从全部 Pass 的 Material cbuffer 中一致排除，编译时通过常量/宏使用；普通属性保持 uniform。静态属性不改变固定资源布局，禁止用条件宏删 cbuffer 字段或绑定。
- 每个 Pass 显式列出 features，VariantKey 由该 Pass 使用的 `(PropertyId, value)` 排序得到。示例中 Forward 是 2×3=6 个程序组合，Shadow 是 2 个，共 8 个 Pass 组合；程序再分别编译所需后端/阶段。
- render/state 可以读取同一静态属性的 CPU 值；例如 AlphaClip 同时影响队列和裁剪程序。Surface/Blend 等未标 keyword 的状态输入改变常量和 PSO，不触发 Shader 编译。
- 首期限制每 Pass 最多 64 个静态组合，包内输出总组合数与编译数量。超额报错并要求作者减少轴，不在运行时隐式扩张。资源包收集准确组合；缺失时明确诊断，不选择“最接近”变体。
- 属性改名保持 ID 不变即可保留材质值；enum 显式数值是序列化契约，改标签不能改值。删除枚举项、错误类型或非法值必须报告，热重载按整代校验发布。

移除作为独立真相的任意 `MaterialAsset.variant` 字符串；可读变体标签只是诊断显示。没有隐式全局 keyword 覆盖。编辑静态属性走已有后台准备/帧边界发布链路，Execute 不编译或创建 PSO。等待期间维持完整旧快照，不能出现新队列/旧程序的混合状态。

## 跨文件 HLSL 与参数模块

把绑定适配与算法库分开：入口适配层可读取生成的 GPU 绑定；库函数只通过显式参数得到数据。GPU cbuffer/纹理仍需要绑定声明，目标是消除库的隐式依赖，而不是承诺所有 HLSL 都没有全局对象。

```hlsl
float3 TransformObjectToWorld(float3 positionOS, ObjectData objectData);
float4 SampleBaseColor(Texture2D<float4> baseMap, SamplerState baseSampler,
                       float4 factor, RasterSamplingContext sampling);
float3 EvaluatePBR(SurfaceData surface, LightingContext lighting);
float EvaluateShadow(ShadowData shadow, Texture2D<float4> shadowMap,
                     float3 positionWS, float3 normalWS);
```

Math/BRDF/Surface 等值计算库不 include 绑定文件；Lighting/Sampling 的资源参数也显式传递。入口从生成适配层取得 ObjectData/LightingContext，调用表面、光照和输出编码函数。纹理对象先用函数独立参数传入，首期不依赖含资源成员的结构体；SM6.0 DXIL 与 SPIR-V 的可编译性纳入双后端验收。

光栅 RasterSamplingContext 显式携带梯度，在 AlphaClip/discard 前计算；未来光追用独立显式 LOD/梯度上下文，不让共享库隐式执行 ddx/ddy。

`.params` 从声明文本复用升级为明确的接口模块：

1. 每个标准块定义稳定 SchemaId、版本、字段类型和生成布局 hash，生成 C++ 数据/打包接口与 HLSL 类型。View/Object/Lighting/Shadow 分块，Unlit 只声明真正需要的块。
2. Shader 通过 use 引用模块，可自由取 HLSL 局部绑定别名；别名不重复实例化同一模块。标准资源声明携带类型化语义，例如主光源阴影、IBL 图集；这类语义是引擎 API，不由资源变量拼写推断。
3. 编译/加载将 SchemaId、资源语义与 PropertyId 解析为代际 BindingHandle。Renderer 一次验证所需版本/布局，再按 handle 绑定；帧内不逐字段比较 `Frame`，也不 `if (symbol == "MainLightShadow")`。
4. 项目自定义块由调用方显式提供生成的接口 handle，Renderer 不猜内容。未知必需语义、接口冲突或重复绑定报错；可选资源必须声明默认策略，不偷偷补占位资源。
5. Graph 构建先把 handle 对应到资源版本并声明访问，再冻结绑定快照。接口语义不绕过 Graph，也不成为全局可变纹理仓库。

稳定属性 ID 继续承担材质序列化身份；另加可选、类型化导入语义（BaseColor、NormalMap 等），将 glTF 属性映射到匹配接口。导入器不要求作者将属性命名为 `base_color`，未声明语义的自定义属性由作者赋值。

## Stencil、PSO 与 Graph 的闭环

D32 没有模板面，Stencil 使用新增的可检测 D32S8 格式，配套 DX12/Vulkan 的视图、PSO、屏障和命令映射；设备不支持时明确报告。

- 先解析材质/Pass 状态，汇总当前视图所需附件，再选 Depth/DepthStencil 格式，最后准备对应 PSO，避免准备完 D32 PSO 才发现需要模板。
- PSO Key 包含不可变 Raster/Depth/Blend/Stencil 字段及真实附件格式。reference 为每 Draw 动态状态，不进入 PSO Key；每个 PreparedDraw 显式设置，包括从上个 Draw 的非零值恢复为零。
- Graph 分别声明 Depth/Stencil 的读取、写入和 Load/Clear，正确保留旧值依赖。深度只读不代表模板只读；模板写掩码也不代表完整覆盖。首期允许保守跟踪整个组合附件，但实际后端访问状态和各面的初始化语义必须正确。
- Opaque/Transparent Feature 根据最终状态声明所需访问，不能用阶段名称替代访问声明。需要 Stencil 写的透明 Draw 不得进入只读模板附件配置。
- 沿用 GPU generation 与录制快照保活、有界弱引用 PSO 缓存及真实完成点退休；增加状态种类不能恢复无限强引用缓存。

首期不扩展模板采样/复制、多颜色附件、双源混合或任意深度偏置表达式。DepthBias 等状态在有明确用例时按同一模型加入。

## 迁移与验证原则

这是尚未发布接口的有版本替换，不建设两套长期并存的路径。类型化运行时契约、作者入口、内置/项目 Shader、示例材质、导入桥和工具使用同一模型。旧具名变体与材质 v1 明确拒绝，迁移时按 [显式映射](../docs/shader-assets.md) 转换为属性值，不通过名称包含 Transparent 等启发式猜测，也没有自动迁移器。缓存/格式版本随接口更新，Library 可重建。

验收必须证明：任意重命名 Pass、属性 symbol、绑定别名与 HLSL 文件后，保持资源/属性/接口 ID 时行为不变；状态更改不编译程序，静态轴更改选择准确程序；混合颜色/Alpha、Reversed-Z、独立 depthWrite、模板双面操作及动态 reference 隔离正确。模板遮罩和读取场景覆盖 Load/Clear、跨 Pass 保留和 Graph 剔除，双后端 Validation 为零。M4 PBR/HDR/IBL/阴影/透明、热重载、窗口交互和独立 Player 均需回归，不以文档完成代替实现验收。

## 参考与取舍

参考 Unity 的 [Blend](https://docs.unity3d.com/6000.0/Documentation/Manual/SL-Blend.html)、[Stencil](https://docs.unity3d.com/6000.0/Documentation/Manual/SL-Stencil.html) 状态组织、[关键字属性](https://docs.unity.com/en-us/engine/6000.0/manual/materials-and-shaders/shaders/writing-custom/shader-writing/sl-multiple-program-variants/shader-keywords-material-inspector) 和 [URP Lit](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/Shaders/Lit.shader) 的 Surface/Blend 策略；[URP Lighting](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/ShaderLibrary/Lighting.hlsl) 的结构体参数求值支持本提案的库接口方向。

Unity 本身也使用名称标签、关键字与全局资源。RenderLab 借鉴其职责划分，再以类型化 role、接口身份和显式函数参数减少本项目的隐式耦合；不宣称照搬 Unity 就能消除名称约定。
