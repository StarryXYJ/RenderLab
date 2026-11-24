# 项目原生渲染模块

模块注册入口为 `renderlab_project_module_v1`，定义见 `src/renderer/project_module.h`；返回的静态描述包含模块 ID、节点及管线。管线当前声明 Forward 基底和有序节点 ID，可引用同一加载集合中其他模块的节点。ID 限于 128 字节以内的小写 ASCII 字母、数字、`.`、`-`、`_`。注册器拒绝缺失入口、版本或构建配置不匹配、重复 ID、无效描述和缺失节点；一次加载集合失败不发布部分注册结果。

模块需与宿主使用同一 RenderLab 源码、MSVC 编译器、CRT 和 Debug/Release 配置构建。入口是 C ABI；未来调用节点时，Graph、RenderFrame 和 Runtime 是同构建的借用 C++ 对象，不构成跨版本 C++ ABI。版本检查包含显式模块/Graph ABI、MSVC 版本、CRT 和迭代器调试级别，但不能自动证明源码或所有编译选项一致；这些接口变化时必须同步提升 Graph ABI 并重编模块。节点回调返回零表示成功，失败通过有界 UTF-8 缓冲报告，不得向宿主抛异常。

`ProjectModules` 拥有模块 DLL，查询返回的引用和函数指针仅在该对象存活期间有效。宿主在调用节点前必须保证模块存活，退休所有节点回调与在途 GPU 工作后才可销毁它。节点不得跨帧保留临时 Graph、帧引用或 Graph 句柄；Graph 内回调所捕获的资源与代码需覆盖整个执行生命周期。

当前只实现并测试模块注册器，Editor、Player 和场景 Renderer 尚未加载或执行项目模块，也没有用户可选择的项目管线。样例测试模块在 `tests/project_module_fixture.cpp`；实际渲染节点、模块路径与运行描述属于后续阶段。测试入口：构建 `renderlab_project_module_tests` 后运行 `ctest -R ^project_module.contracts$ --output-on-failure`。
