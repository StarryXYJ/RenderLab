# 参数接口、生成代码与绑定身份

参数的结构契约由 `.params` 声明，HLSL 变量名是局部引用。Schema ID/版本/布局与具体绑定槽、别名、程序代际分别管理。

```text
[scope(view), schema("project.camera", 1), type(CameraData)] cbuffer Camera
{
    [id("projection")] float4x4 Projection;
}
```

`scope` 指定 View/Draw/Pass 生命周期范围，`schema` 给出稳定 ID 与正整数版本，`type` 指定生成数据类型名称。Schema ID 不能含控制字符；type 使用合法标识符。省略 type 时使用块名加 Data，公共接口宜显式命名。块仍只允许数值字段。

`use "Camera.params" as Main;` 引入模块并把新引入块的绑定别名变为 `Main_Camera`，类型保持 CameraData。alias 不用于重复实例化已加载模块。字段改名时保留显式 PropertyId；调整接口语义时维护版本。

## HLSL 入口与库

`<renderlab/generated/types.hlsli>` 只生成 enum 常量和数值结构体，不含 cbuffer/纹理/采样器全局绑定。库 include 此头，使用函数参数接收值；入口 include `<renderlab/generated/parameters.hlsli>` 获得绑定适配，再显式调用库。

```hlsl
// Library: no implicit camera binding.
#include <renderlab/generated/types.hlsli>
float4 Project(float3 p, CameraData camera)
{
    return mul(camera.Projection, float4(p, 1));
}
// Entry includes parameters.hlsli and calls Project(position, Main_Camera).
```

类型头与绑定头分别进入编译缓存身份，DXC 的 Include 服务只为本次编译提供对应生成内容。反射仍检查本地生成符号、槽、类型和布局；Renderer 不应以这些符号拼写猜测数据用途。

## C++ 数据与打包

`renderlab_shaderc FILE.shader --cpp OUTPUT.hpp` 为带 schema 的块生成：

- `renderlab::shader::generated` 下的数值结构体，32 位标量和扁平 std::array；矩阵输入按行排列。
- 每个类型的 constexpr BlockContract，含 Schema ID、版本、布局 hash 和 scope。
- 重载 Pack，返回填零、256 字节对齐大小的字节数组，按生成偏移处理列主矩阵和数组 stride，不依赖 C++ 结构体 padding。

打包验证非有限浮点、数值范围、bool 0/1 与 enum 合法代码。同一声明生成 CPU 数据、PackingPlan 和 HLSL 类型；跨 Pass 导出同一 Schema 时结构冲突会报错。输出内容未改变时保留生成头时间戳。

布局 hash 包含 scope、稳定字段 ID、数值类型/形状、枚举代码、偏移、大小和 stride；排除块绑定别名、字段 HLSL symbol、数据类型显示名称与绑定槽。相同布局不能代替语义版本，必须同时校验 ID/version/hash/scope。

## 绑定 handle

Package 以 PassHandle 解析 BindingHandle，handle 包含所属包/Pass、接口索引和常量块/资源种类。GetBlock/GetResource 校验代际、范围和种类；不能跨热重载复用。

- SchemaBinding(pass, contract)：按稳定 Schema ID 找块，校验版本/布局/scope；未声明该接口返回空，不兼容时报错。
- SemanticBinding(pass, semantic)：按固定资源语义找接口；`[binding(EnvironmentAtlas/BrdfLut/MainLightShadow)]` 只允许外部 sampled texture，同语义重复声明报错。
- BlockBinding/ResourceBinding(pass, localName)：自定义调用方显式选择一个局部接口，名称仅在解析时使用，不形成 Renderer 行为约定。

宿主为 Shader 明确声明的接口提供数据。资源绑定依然必须先在 Graph 声明访问与版本，handle 不绕过同步、生命周期或不可变快照。场景适配与库迁移的实施记录见 [阶段计划](../design/phaseplan.md)。

验证入口 `parameters.contracts` 实际编译生成头，并与通用 PackingPlan 及双目标 HLSL 反射对照；覆盖模块/资源改名、错误版本/代际、矩阵/数组/bool/enum/范围与 NaN。`shader.contracts` 与双后端材质/阴影回归同时保留。
