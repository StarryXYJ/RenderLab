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

模块需与宿主使用同一 RenderLab 源码、MSVC 编译器、CRT 和 Debug/Release 配置构建。入口是 C ABI；调用节点时，Graph、RenderFrame 和 Runtime 是同构建的借用 C++ 对象，不构成跨版本 C++ ABI。版本检查包含显式模块/Graph ABI、MSVC 版本、CRT 和迭代器调试级别，但不能自动证明源码或所有编译选项一致；这些接口变化时必须同步提升 Graph ABI 并重编模块。当前 Graph ABI 为 2。节点回调返回零表示成功，失败通过有界 UTF-8 缓冲报告，不得向宿主抛异常。

`ProjectModules` 拥有模块 DLL，查询返回的引用和函数指针仅在该对象存活期间有效。`SceneRenderer` 持有模块，并让项目图及其提交保留模块：图的回调和资源销毁后、在途命令退休后才释放相应所有权，包括构图或提交失败路径。调用方仍须遵循 Runtime 的线程及 GPU 资源退休契约；设备丢失恢复不在本次验证范围。节点不得跨帧保留临时 Graph、帧引用或 Graph 句柄；Graph 内回调需按值捕获所需数据并遵循 Graph/RHI 资源所有权约定。

`SceneRenderer` 的 `SceneRenderOptions.projectPipeline` 可指定已注册项目管线，当前只支持 Forward 基底：先构建内置 Forward，再按节点声明顺序插入 Pass，最后构建显示及诊断 Pass。节点通过借用的 Graph 声明资源访问，例如读写 `CameraColor`；Graph 负责依赖、剔除和同步，节点顺序边不会保活无输出的 Pass。节点失败或管线不存在会报告错误，调用方可继续使用内置管线。设备能力声明和其他基底尚未提供。

`sample/m4/ExperimentSource` 是项目自行提供 CMake 目标和节点的可运行示例，通过可选的 `RENDERLAB_PROJECT_SOURCE_DIR` 加入同一引擎构建；该目录中的 CMake 脚本会在配置时执行，只应指定可信项目。示例把 DLL 放到项目 Library，构建与运行步骤见 [手册](../manual/build.md#项目渲染模块示例)。切换项目仍只是加载已有模块，未实现自动构建或模块热重载。

独立 DLL 的图执行测试在 `tests/project_render_fixture.cpp`；注册测试在 `tests/project_module_fixture.cpp`。构建 `renderlab_project_render_tests`、`renderlab_project_module_tests` 后，运行 `ctest -R "^(project_module.contracts|gpu\.(dx12|vulkan)\.project_render)$" --output-on-failure`。
