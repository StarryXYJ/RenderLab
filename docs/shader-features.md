# 类型化属性与 Pass 静态轴

`.shader` 的逻辑材质属性与 GPU 字段分开：`MaterialTemplate::Properties()` 返回全部属性，`Fields()` 只返回实际常量缓冲字段。普通 bool 使用 uint32 uniform；enum 使用显式 uint32 值，具有独立类型身份、默认值和可供工具读取的选项。

```text
enum NormalMode { Off = 0; Tangent = 3; Detail = 7; }
material
{
    [id("clip"), keyword("ALPHA_CLIP")] bool Clip = false;
    [id("normal"), keyword("NORMAL_MODE")] NormalMode Normal = Tangent;
    NormalMode DynamicMode = Off;
    bool Enabled = true;
}
pass Beauty
{
    features = { Clip, Normal };
    vertex = "Surface.hlsl:VSMain";
    pixel = "Surface.hlsl:PSMain";
    depth = false;
}
```

本页只说明已接入的属性/变体机制；Pass role 和完整 state 作者语法随 [阶段计划](../design/phaseplan.md) 后续修订。

## 规则

- enum 项必须显式给出唯一 uint32 数值，最多 64 项。数值是保存契约，改标签不应改数值；默认值可以写项名，省略时用第一项。
- `[keyword("MACRO")]` 只允许用于材质 bool 或 enum 标量，宏必须是合法标识符。普通 bool/enum 不生成变体，UI 元数据与编译选择互不隐含。
- 静态属性按稳定 PropertyId 保存一次，在所有 Pass 的 Material cbuffer 中一致排除；不允许条件编译删除普通字段或绑定。
- 每个 Pass 显式声明消费的 features，作者 symbol 在加载时解析成 PropertyId，并按 ID 排序。其 VariantKey 是有序的 `(PropertyId, uint32)` 列表；可读 label 只是诊断文本。
- bool 生成 0/1 宏，enum 生成一个数值宏。生成参数头提供 `NormalMode_Off`、`NormalMode_Tangent` 等数值宏，供普通 HLSL 与预处理判断使用；按需 include `<renderlab/generated/parameters.hlsli>`。
- 每 Pass 最多 64 个组合，超过时在预处理/编译前报错。编译包按所需后端和阶段生成程序；Forward 使用两个轴为 6 个组合，Shadow 只用 Clip 为 2 个，而非每个 Pass 都编译全部轴。
- 运行时从已验证材质值选择准确键；缺少/非法值、缺失组合都报错，不选择近似程序。静态编辑走准备与整代快照发布，Execute 不编译程序。

`SchemaToJson` 的 properties 输出全部逻辑属性及 keyword/choices，每个 Pass 的 features/variants 输出真实组合。静态属性改 symbol 但保持 ID，可以保留材质值和选择键；删除 enum 数值会在迁移时拒绝旧值。

现有 M2/M4 的具名变体资产仍处于分步迁移中；同一个 Shader 禁止将具名变体与 keyword 属性混用。旧入口与资产在后续全量迁移步骤删除，不作为新 Shader 的设计方向。

验证入口：`shader.contracts` 对双目标编译、布局、组合预算、精确查找、保存与改名/枚举迁移作检查；`gpu.*.render_state` 通过静态 bool 改变片元输出并读回像素，验证实际 Material Runtime 程序选择，静态属性独占材质不分配常量 Buffer。
