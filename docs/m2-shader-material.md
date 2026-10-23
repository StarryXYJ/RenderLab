# Shader 与材质

M2 提供从声明、编译、反射、材质值到 GPU 快照的最小链路。阶段结果以 [phaseplan](../design/phaseplan.md) 为准，命令见 [构建手册](../manual/build.md#shader-与材质工具)。示例源文件位于 `shaders/m2`。

## 声明与 HLSL

`.shader` 是唯一手工维护的 Shader 元数据源。HLSL 编写算法，`.params` 复用显式常量块；材质 `.material` 和编译缓存采用版本化 JSON，但不另设手写 JSON Shader 格式。

```text
shader "Example/Surface"
{
    use "shared/view.params";
    material
    {
        [id("base_color"), label("基础色"), color, group("表面")]
        float4 BaseColor = (1, 1, 1, 1);
        [range(0, 1)] float Roughness = 0.5;
        [srgb] Texture2D<float4> BaseMap = white;
        SamplerState BaseSampler = point_clamp;
    }
    variant Default { DETAIL = 0; }
    variant Detail { DETAIL = 1; }
    pass Forward
    {
        vertex = "surface.hlsl:VSMain";
        pixel = "surface.hlsl:PSMain";
        depth = false;
        cull = false;
    }
}
```

`view.params` 只声明共享 cbuffer 和其他 `use`，不包含 Shader、材质、变体或 Pass：

```text
[scope(view)] cbuffer View
{
    float4x4 ViewProjection = (1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
    float4 CameraPosition = (0, 0, 0, 1);
}
```

所有路径相对所在声明文件解析。`use` 去重并报告环。常量块支持 `view`（`frame` 为同义词）、`draw`、`pass`；省略 scope 使用 pass。一个 Shader 仅一个 material 块。资源在 material 或 Pass 的 parameters 内声明；显式 cbuffer 只允许数值字段。

```text
pass Noise
{
    parameters
    {
        uint Seed = 7;
        StructuredBuffer<float4> Input;
        RWStructuredBuffer<float4> Results;
        [format(rgba8_unorm)] RWTexture2D<float4> Output;
    }
    compute = "noise.hlsl:CSMain";
}
```

HLSL 引用每次编译独立生成的虚拟头文件，再正常 Include 工具代码：

```hlsl
#include <renderlab/generated/parameters.hlsli>
#include "tools/noise.hlsli"

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = MakeNoise(id.xy, NoiseParameters.Seed);
}
```

生成文件提供 `ConstantBuffer<MaterialData> Material`、`View`、`NoiseParameters` 和资源 symbol。不要再在 HLSL 重复声明这些参数；虚拟文件不落盘，不通过全局 include 目录覆盖，也不跨并发编译共享可变内容。普通 HLSL Include 支持嵌套目录和 CompileOptions.includeDirectories，实际读取的内容参与缓存键。共享算法仍使用普通 `.hlsli`。

## 类型、元数据与布局

| 类别 | M2 支持 |
| --- | --- |
| 数值 | float/int/uint/bool；对应 2/3/4 分量向量；float2x2 到 float4x4 的方阵与非方阵 |
| 数组 | 数值类型一维定长数组，1 到 256 元素 |
| 纹理 | Texture2D<float4>、RWTexture2D<float4>；写入纹理必须声明 rgba8_unorm |
| 缓冲 | StructuredBuffer、RWStructuredBuffer；元素为 float/int/uint 标量或 2/4 分量向量 |
| 采样器 | SamplerState；示例资源解析器提供 point_clamp |

缓冲暂不支持三分量元素、自定义 struct、矩阵元素和资源数组。三分量结构化元素需要单独处理跨后端对齐或启用额外 Vulkan 功能，不能由 Shader 编译参数默默改变设备能力。material 不允许 UAV 输出；输出放在 Pass 参数中。

属性可声明 `id`、`label`、`group`、`tooltip`、`unit`、`range`、`enum`、`color`、`srgb`、`hdr`、`format`。id 默认为 symbol；持久资产建议显式指定 id。显示名和 symbol 的改名不应改变稳定 id。默认数值缺省为零，单个数值可以广播给复合类型。enum 用 int/uint 数值索引，range/default/类型范围会在编译、编辑和打包时校验。color 适用于 float3/4，srgb 适用于采样纹理。

元数据来自声明，反射只验证 GPU 接口。Shader 优化掉的字段仍保留默认值、稳定 id、UI 元数据和布局；不能用反射结果裁掉材质属性。`MaterialTemplate` 保持 Package 的 shared_ptr，只投影已有数据，不复制维护另一套 Schema。

固定逻辑 space：

| Scope | DX12 register space / Vulkan set |
| --- | --- |
| View / Frame | 0 |
| Material | 1 |
| Draw | 2 |
| Pass | 3 |

每个 space 内先按声明顺序分配公共常量块和资源，再分配 Pass 局部参数；所有描述符类型共用 slot 序列。DX12 使用对应 b/t/u/s 寄存器类别，Vulkan 使用显式 vk::binding。固定公共声明让同一材质跨 Pass、变体和目标共享一份数值布局。即使资源被优化掉，BindingLayout 仍保留声明的槽位。

常量使用 HLSL cbuffer 打包：标量 4 字节，向量不跨 16 字节边界，数组元素从寄存器边界开始，矩阵按列存储且列 stride 为 16 字节。后续字段可以占用最后数组元素或矩阵列剩余的空间。`FieldLayout` 保存 offset、size、arrayStride、matrixStride，块大小向上取整到 16 字节；实际 CBV 存储/范围再对齐到 256 字节。每块最多 16KiB，当前每个接口最多 32 个绑定。

CPU 的逻辑矩阵值按行提供。`material::Pack` 按 PackingPlan 转成 GPU 列存储，bool 生成 uint 并写入 32 位 0/1。Shader 使用 `mul(matrix, columnVector)`。DXC 的 SPIR-V 矩阵类型和 major decoration 与 HLSL 表示相反，反射层归一化后校验；双后端 Compute 实际读取非方阵元素作最终验证。

## 编译边界与缓存

`CompileShader(path, options)` 是 CPU 同步操作，可在后台调用，返回 BuildResult。成功结果为 `shared_ptr<const Package>`，含定义、完整接口、全部有限变体/Pass/入口/目标程序、依赖路径、内容 key 和 generation；失败返回 diagnostics，不创建 RHI 对象。SM 6.0、HLSL 2021、严格模式、警告作为错误、列主序；SPIR-V 目标 Vulkan 1.3，采用 DX 布局。

DXIL 通过 DXC 容器反射，SPIR-V 通过固定版本 SPIRV-Reflect 验证绑定身份、类别、常量偏移、维数、数组和矩阵 stride。用户自行声明的未授权资源会被拒绝。声明负责未使用参数的元数据，反射负责实际编译接口验证。

缓存 key 包含声明和模块内容、实际 Include 内容、预处理源码快照、宏、入口/目标、生成头、生成器/固定编译选项及 DXC 版本/提交号。编译使用同一预处理快照；读写期间变更磁盘源文件不会让某个程序的缓存键对应另一份程序内容。缓存命中仍预处理并校验反射，跳过目标编译；清单和每个二进制的 SHA-256 不一致时重建。临时文件在同目录完成写入后替换发布。

变体是最多 16 个具名有限宏集合，每个 Shader 最多 16 个 Pass。没有隐式 keyword 笛卡尔积，也不在 Draw/Execute 中编译缺少的组合。新增编译选项需同步更新编译器参数与缓存版本。缓存内容包含本机路径，因此属于本机可重建产物，不是跨机器发行包。

## 材质准备与 Pipeline

`material::Asset` 只包含 Shader 逻辑引用、variant、按稳定 id 保存的类型/数组形状/数值以及资源逻辑引用。Save/Load 不保存物理 offset、描述符或 GPU 指针。资源引用必须为非空逻辑名或相对路径；资源查找交给调用方 ResourceResolver，M2 不建立 AssetDatabase。

```cpp
auto build = shader::CompileShader(path, options);
if (!build) throw std::runtime_error(build.diagnostics);
shader::MaterialTemplate schema(build.package);
auto asset = material::Create(schema);
material::Set(asset, schema, "base_color", std::array<double, 4>{1, .2, .1, 1});
material::Save(asset, materialPath);

material::Runtime materials(rhiRuntime, target);
material::Preparation prepare;
prepare.pass = "Forward";
prepare.vertexStride = 20;
prepare.attributes = {{"POSITION", 0, rhi::VertexFormat::Float3, 0},
                      {"TEXCOORD", 1, rhi::VertexFormat::Float2, 12}};
auto snapshot = materials.Prepare(build.package, asset, prepare, resolveResource);
```

`Preparation.constants` 按块名提供 View/Draw/Pass 数值，字段按稳定 id 索引，缺省使用声明默认值。`Preparation.bindings` 按生成的 HLSL symbol 提供 Pass 资源，也可提供已经打包的外部 cbuffer。结构化缓冲必须提供匹配元素的 stride；sRGB 属性必须解析到 sRGB 纹理。

Prepare 必须在 Runtime 创建线程执行。它复用当前代的 ShaderModule/BindingLayout，通过 RHI 唯一 `CreatePipeline` 缓存入口准备 PSO，打包数值并创建不可变 BindingGroup。Prepared 持有代际、PSO、绑定和新常量缓冲；数值或纹理修改产生新快照，正在执行的快照不原地改写。

普通参数改变只更新常量/绑定，静态功能通过 variant 选择程序，depth/blend/cull 等渲染状态来自 Pass 或 Preparation 显式覆盖。PSO key 包含不可变 Shader/布局身份、全部顶点属性/stride、颜色格式和全部当前渲染状态。新代创建新模块身份，不能命中旧代 PSO；数值变化不进入 key。缓存最多 256 个弱引用，GPU 代查找最多 64 个弱引用，淘汰查找项不影响仍被命令保活的对象。超限后再次请求可能重建，不保证永久命中。

调用方显式录制 `snapshot.constants` 的 HostWrite → UniformRead 屏障，再绑定 Pipeline/BindingGroup。外部上传常量、只读缓冲、UAV 和纹理状态也由调用方负责；Prepare 不插入屏障，不维护第二套资源状态机。Execute 只使用准备好的对象录制命令。完整样例见 `src/samples/material_demo.cpp`。

## 热重载事务

1. 侦测到依赖文件变化后调用 ReloadCompiler.Request；Package.dependencies 提供跟踪路径。
2. 单个 jthread 在后台预处理/编译。连续请求合并，正在编译的旧代结束后丢弃；Poll 只交付最新请求结果。
3. Owner thread 获取成功包，LiveMaterial.Stage 迁移值并准备调用方列出的全部必需 Pass。Shader 编译失败、类型冲突、缺失 Pass 或 GPU 准备失败都不替换当前状态。
4. 在调用方选择的帧边界调用 PublishFrameBoundary，整体换入新包、Asset 和快照。也可以 Stage 同一代与编辑后的 Asset，沿用相同的发布规则。
5. 旧命令保活其中的 PSO、Shader、布局、BindingGroup 和资源。录制未提交的命令可以继续提交，实际完成后由 RHI 退休；没有热重载时强制 DeviceIdle。

稳定 id、类型、数组形状一致时保留旧值；新增属性采用默认值，删除属性移除。类型变化（含数值/资源转换和资源元素类型）、值不满足新 range 或 variant 删除会报告冲突。失败继续使用旧包，不静默丢弃用户值。Stage 失败保留已存在的有效待发布状态；应用可以选择继续发布它或等待下一次成功 Stage。显式旧 generation 不能覆盖较新 active/staged generation。

M2 提供编译请求与帧边界接口，不附带操作系统文件监视器、UI 编辑器、GPU PSO 后台创建或旧代编译的中途强制取消。退出会等待当前 CPU 编译结束；不会先销毁工作线程仍在访问的数据。

## 职责与验收入口

| 文件组 | 职责 |
| --- | --- |
| shader/lexer、types、parser、parameter_parser | 声明、源位置、共享模块和参数规则 |
| shader/layout、value、schema | 唯一布局、逻辑数值校验和 Schema 导出 |
| shader/dxc_compiler、dxil_reflection、spirv_reflection | 编译与目标反射适配 |
| shader/compiler、reload | 内容缓存、编译任务组合与后台代际 |
| material/asset、packing、serialization | 逻辑值/迁移、CPU 打包与保存加载 |
| material/runtime、live_material | GPU 准备与事务发布 |
| rhi/pipeline_cache | 唯一有界 PSO 查找入口 |

`shader.contracts` 测声明、元数据保留、数组/矩阵布局、双目标反射、缓存失效/损坏、材质保存与迁移、连续后台请求和编译失败恢复。`gpu.{dx12,vulkan}.material` 测同一材质多 Pass、参数/纹理编辑、sRGB、variant/状态缓存，以及带非零 offset 的 cbuffer/SRV/UAV、Compute 实际矩阵值、写后写同步、录制/在途热重载和回收。

验收样例输出 default/edited/reloaded BMP、sample.material、日志和临时重载副本。源码不被测试修改；双后端验证开启，Shutdown 后检查验证错误及显存占用。M1 的固定噪声和窗口测试继续独立运行，避免材质层掩盖底层回归。
