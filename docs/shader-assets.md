# Shader 资产身份与材质 v2

Shader 的稳定身份来自 `.shader.meta` 的 `id`，格式为 32 位小写十六进制。材质 v2 的 `shader` 保存此 ID，Shader 声明名称仅用于显示和诊断。移动 Shader 时同时移动 sidecar，并同步相对 entry/include 路径；不改变 ID 即可保持引用。复制为新资产应分配新 ID。

```json
{
  "version": 1,
  "id": "5c17b47011bf555285a61b887634f8d1",
  "dependencies": {},
  "subresources": {}
}
```

SceneRenderer 扫描引擎 Shader sidecar 建立 ID 索引，与项目 AssetDatabase 使用同一材质引用规则。重复 ID、引擎与项目冲突、非法 ID 或材质 `.meta` 中 `dependencies.shader` 与正文不一致均报错。默认 Lit/显示变换只固定资源 ID，不限制资产显示名、路径或 HLSL symbol。

`CompileOptions.assetId` 可由资源系统显式提供；存在 sidecar 时必须相符。编译器把 ID、sidecar 内容与依赖计入缓存身份。独立工具未提供 ID 且没有 sidecar 时，以 canonical 源路径的 SHA-256 前 32 位生成临时 ID，适合测试和临时预览；移动路径会改变该 ID。持久资产必须登记 sidecar。

## 材质保存与迁移

`.material` 使用 version 2，只保存 Shader ID、按稳定 PropertyId 索引的数值和资源引用。静态 bool/enum 与普通属性使用同一 values 集合，不再保存独立 variant 字符串。物理偏移、绑定槽和 GPU 对象不进入文件。

v1 和旧 `variant {...}` 作者语法均明确拒绝，没有运行时兼容分支。迁移既有文件时：

1. 为 Shader 登记/保留 `.meta` ID，把材质 shader 名称替换成对应 ID，version 改为 2。
2. 删除 variant 字段，将旧组合显式转为属性值。原 Lit/Unlit 的 Default 为 Surface=Opaque、AlphaClip=false；Masked 为 Opaque、AlphaClip=true；Transparent 为 Transparent、AlphaClip=false、Blend=Alpha。保存的是声明中实际 PropertyId 和数值，不能靠名称包含关系猜测。
3. 将旧纹理键改为实际稳定 ID；内置 Lit/Unlit 当前使用 `base_map`、`metallic_roughness_map`、`normal_map`、`occlusion_map`、`emission_map`、`base_sampler`。
4. 为 Shader 用 `[keyword]` 和每 Pass 的 features 声明静态轴，再验证材质保存/重载、完整画面与分类。

Library 是可重建缓存，生成器版本升级会使旧 Shader 编译缓存失效。清理缓存不迁移用户材质。热重载保留稳定 ID/类型/形状兼容的值，Shader ID 改变或 enum 数值失效会拒绝发布并保留旧快照。

## 类型化导入语义

`[semantic(BaseColor)] float4 Tint;` 把导入语义映射到作者自由选择的属性 ID/symbol，导入器使用 `MaterialSemantic`，不会要求字段叫 base_color。标记仅允许 Material scope、精确类型和一对一映射；标量不能标作纹理语义，数组也不支持。

| 语义 | 类型 |
| --- | --- |
| BaseColor | float4 |
| Metallic、Roughness、NormalScale、OcclusionStrength、AlphaCutoff | float |
| Emission | float3 |
| Surface、Cull、AlphaClip | SurfaceType、CullMode、bool |
| BaseMap、MetallicRoughnessMap、NormalMap、OcclusionMap、EmissionMap | Texture2D<float4> |

`material::Set/SetResource` 同时支持稳定 PropertyId 和 MaterialSemantic。缺少所请求语义时报错；自定义属性由作者设置。glTF 只为没有用户覆盖的默认材质应用导入值，用户 `.material` 保持权威。Schema JSON 的 `materialSemantic` 供工具读取；它与外部资源 `[binding(...)]` 的语义不同，后者用于 Renderer 提供 IBL/阴影等绑定。

状态、静态轴和参数接口分别见 [渲染状态](render-states.md)、[Shader 特性](shader-features.md)、[参数接口](shader-parameters.md)。`shader.contracts` 检查身份/语义/格式边界，双后端 `gpu.*.surface` 在 Shader 名称、路径、Pass、属性 symbol、模块与资源别名全部改名后比较场景像素。
