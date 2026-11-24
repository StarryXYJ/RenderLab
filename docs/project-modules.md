# 项目原生渲染模块

模块注册入口为 `renderlab_project_module_v1`，定义见 `src/renderer/project_module.h`；返回的静态描述包含模块 ID、节点及管线。管线当前声明 Forward 基底和有序节点 ID，可引用同一加载集合中其他模块的节点。ID 限于 128 字节以内的小写 ASCII 字母、数字、`.`、`-`、`_`。注册器拒绝缺失入口、版本或构建配置不匹配、重复 ID、无效描述和缺失节点；一次加载集合失败不发布部分注册结果。

项目清单可选 `rendering`，缺失时保持原项目行为：

```json
"rendering": {
  "version": 1,
  "modules": ["Library/Modules/experiment.dll"],
  "defaultPipeline": "experiment.forward"
}
```

模块路径相对项目根目录，不能越出项目；最多 64 个且不能重复。`defaultPipeline` 使用稳定 ID，`renderlab.` 前缀保留给内置管线。SceneRenderer 加载清单中的模块并验证默认管线存在；模块是宿主进程内的原生代码。Player、Sandbox 和 Editor 使用项目默认管线；Editor 可在现有管线下拉框选择项目管线，显式 `--pipeline` 选择内置管线会覆盖默认值。宿主不会自动构建模块。

项目也可用 `RenderLab.Project.PipelineCatalog` 在 C# 中注册 Forward 管线及原生节点 ID，
通过 `Save` 生成运行描述。清单的 `rendering.pipelines` 是相对项目根目录的描述文件路径：

```json
"pipelines": "Library/experiment-pipelines.json"
```

描述格式为 `{"version":1,"pipelines":[{"id":"example.managed-blue","base":"forward","nodes":["example.clear"]}]}`。
注册器将其与模块中的管线合并；管线 ID 不得重复，节点必须由所加载的原生模块提供，
每个文件最多 256 条管线、每条最多 256 个不重复节点，文件最大 1 MiB。读取、版本、
路径、ID 或节点错误会使项目加载失败；不会发布部分注册结果。生成文件属于可重建的
Library，构建和运行之前须执行项目自己的导出程序。Editor 和 Player 读取相同文件，
不会执行 C# 代码或自动重建导出；修改描述后需重新打开项目。只有 Forward 基底，
这套接口描述节点组成和顺序，不直接在 C# 中调用 RHI 或定义 C++ 节点实现。

`RenderLab.Project.SettingsSchema<T>` 为项目配置 DTO 提供版本化值快照：公共可读写属性用
`[ProjectSetting("stable.id")]` 注册，支持 `bool`、`int`、`float`、`double` 和非空
`string`；数值可指定 `Minimum`/`Maximum`，DTO 可实现 `IProjectSettingsValidator` 做
跨字段校验。ID 使用与管线相同的小写 ASCII 规则，最多 256 项。`CreateDefaults()`
创建默认实例，`Apply` 在副本上逐层应用同版本的 `{"version":1,"values":{...}}`，
`Serialize` 输出所有字段；未知或重复 ID、错误类型、无效数值及版本不匹配会被拒绝，
原 DTO 不受影响。快照限 64 KiB。当前这是独立托管数据接口；项目作用域加载、UI、
持久化和向渲染节点传值尚未接入，实施状态见 [阶段计划](../design/phaseplan.md)。

模块需与宿主使用同一 RenderLab 源码、MSVC 编译器、CRT 和 Debug/Release 配置构建。入口是 C ABI；调用节点时，Graph、RenderFrame 和 Runtime 是同构建的借用 C++ 对象，不构成跨版本 C++ ABI。版本检查包含显式模块/Graph ABI、MSVC 版本、CRT 和迭代器调试级别，但不能自动证明源码或所有编译选项一致；这些接口变化时必须同步提升 Graph ABI 并重编模块。当前 Graph ABI 为 2。节点回调返回零表示成功，失败通过有界 UTF-8 缓冲报告，不得向宿主抛异常。

`ProjectModules` 拥有模块 DLL，查询返回的引用和函数指针仅在该对象存活期间有效。`SceneRenderer` 持有模块，并让项目图及其提交保留模块：图的回调和资源销毁后、在途命令退休后才释放相应所有权，包括构图或提交失败路径。调用方仍须遵循 Runtime 的线程及 GPU 资源退休契约；设备丢失恢复不在本次验证范围。节点不得跨帧保留临时 Graph、帧引用或 Graph 句柄；Graph 内回调需按值捕获所需数据并遵循 Graph/RHI 资源所有权约定。

`SceneRenderer` 的 `SceneRenderOptions.projectPipeline` 可指定已注册项目管线，当前只支持 Forward 基底：先构建内置 Forward，再按节点声明顺序插入 Pass，最后构建显示及诊断 Pass。节点通过借用的 Graph 声明资源访问，例如读写 `CameraColor`；Graph 负责依赖、剔除和同步，节点顺序边不会保活无输出的 Pass。节点失败或管线不存在会报告错误，调用方可继续使用内置管线。设备能力声明和其他基底尚未提供。

`sample/m4/ExperimentSource` 是项目自行提供 CMake 目标和节点的可运行示例，通过可选的 `RENDERLAB_PROJECT_SOURCE_DIR` 加入同一引擎构建；该目录中的 CMake 脚本会在配置时执行，只应指定可信项目。示例把 DLL 放到项目 Library，构建与运行步骤见 [手册](../manual/build.md#项目渲染模块示例)。切换项目仍只是加载已有模块，未实现自动构建或模块热重载。

独立 DLL 的图执行测试在 `tests/project_render_fixture.cpp`；注册测试在 `tests/project_module_fixture.cpp`。构建 `renderlab_project_render_tests`、`renderlab_project_module_tests` 后，运行 `ctest -R "^(project_module.contracts|gpu\.(dx12|vulkan)\.project_render)$" --output-on-failure`。
