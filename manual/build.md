# 构建与运行

RenderLab 支持 Windows x64，使用 C++20、MSVC、Windows SDK、CMake 和 Ninja。Sandbox 在 DX12/Vulkan 上展示三角形与 Compute 噪声纹理，支持独立的设备报告和 RHI 读回验收。

## 工具链

本轮验证环境如下，第三方源码按提交固定，构建时自动下载到各构建树的 `_deps`。

| 工具 | 实测版本 |
| --- | --- |
| Visual Studio | Community 2026 18.10.1 |
| MSVC | 19.51.36257.0，工具目录 14.51.36231 |
| Windows SDK | 10.0.26100.0 |
| CMake | 4.3.1-msvc1，项目最低要求 3.28 |
| Ninja | 1.13.2 |
| clang-format | 22.1.3 |
| DXC | 1.9.0.5512（01b62ad47），来自本机 Vulkan SDK 1.4.363.0 |

需要 Visual Studio 的 x64 C++ 工具、Windows SDK 和 CMake/Ninja 组件。源文件使用 `/utf-8`、`/std:c++20`、`/permissive-`、`/Zc:__cplusplus` 和 `/W4 /WX`，不把这些选项传给第三方 Target。当前不使用 C++ Modules，关闭自动模块扫描。

启动 x64 Developer PowerShell 后进入仓库根目录。若当前 PowerShell 尚未初始化开发环境，可使用安装器提供的 vswhere 查找，不把本机绝对路径写进共享预设：

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& "$vsPath/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
chcp 65001
```

使用正常的 UTF-8 控制台进行首次配置。中文 MSVC 在无控制台、输出代码页不一致的环境下可能让 CMake 错误解码 `/showIncludes`，导致 Ninja 丢失头文件依赖。遇到这一情况，应在上述控制台中执行 `cmake --preset windows-debug --fresh` 重新检测。不要只隐藏输出或手工编辑生成的 Ninja 文件。

## 依赖

| 依赖 | 固定 Git 对象 | 用途 |
| --- | --- | --- |
| Vulkan-Headers | `2cd90f9d20df57eac214c148f3aed885372ddcfe` | Vulkan 头文件 |
| volk | `a8da8ef3368482b0ee9b0ec0c6079a16a89c6924` | 从系统 Vulkan Loader 加载函数 |
| WinPixEventRuntime | NuGet `1.0.240308001`，固定 SHA-256 见 `cmake/Dependencies.cmake` | DX12 GPU Marker |

两者对应 Vulkan SDK 1.4.321.0 源码版本，实际运行最低要求 Vulkan 1.3。首次配置需要 Git 和 GitHub 网络访问。依赖来源与授权说明位于各依赖源码的 LICENSE 文件，不在引擎源码中复制或修改它们。

构建需要能同时输出 DXIL 和 SPIR-V 的 DXC。CMake 从 PATH、`VULKAN_SDK/Bin` 或显式 `RENDERLAB_DXC` 路径查找，不在共享配置中保存本机 SDK 路径。完整 Vulkan SDK 不是构建必需，但提供 DXC 与验证层更方便。运行需要显卡驱动提供的 Vulkan Loader；DX12 使用系统运行库和 Windows SDK。严格验证额外需要 Windows Graphics Tools 的 D3D12 Debug Layer，以及 Khronos Vulkan Validation Layer（含 synchronization validation 设置）。

例如在本机安装 SDK 后，配置 `VULKAN_SDK`，或在配置命令后加 `-DRENDERLAB_DXC=C:/你的SDK/Bin/dxc.exe`。本机路径也可放入被忽略的 CMakeUserPresets.json。首次配置还会从 NuGet 下载带哈希校验的 PIX 包；构建会将其 DLL 复制到程序目录。不要仅复制 exe 后删掉同目录的 DLL 或 shaders。

## 配置与构建

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 4
ctest --preset windows-debug

cmake --preset windows-release
cmake --build --preset windows-release --parallel 4
ctest --preset windows-release
```

输出分别在 `out/build/windows-debug` 和 `out/build/windows-release`。默认注册 5 项不需要创建设备的基础测试。预设与依赖版本纳入 Git，本机设置使用被忽略的 `CMakeUserPresets.json`。

完整验收启用 GPU 测试，共 13 项。GPU 测试要求验证层和桌面会话，会短暂打开原生窗口：

```powershell
cmake --preset windows-debug -DRENDERLAB_GPU_TESTS=ON
cmake --build --preset windows-debug --parallel 4
ctest --preset windows-debug

cmake --preset windows-release -DRENDERLAB_GPU_TESTS=ON
cmake --build --preset windows-release --parallel 4
ctest --preset windows-release
```

GPU 测试使用 `--validation on`，层不可用时失败，不静默降级。仅运行基础测试可使用 `ctest --preset windows-debug -LE gpu`，但这不能替代设备验收。

## 运行 Sandbox

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan
./out/build/windows-debug/renderlab_sandbox.exe --help
```

无窗口初始化并导出报告：

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --headless --validation on --report out/reports/dx12.json
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --headless --validation on --report out/reports/vulkan.json
```

默认使用 `--validation auto`，验证层缺失会给出警告并在报告中标记禁用；`on` 要求可用，`off` 明确关闭。能力表表示硬件支持，不代表引擎已启用光追、Mesh Shader 或 GPU Driven。

`--adapter N` 使用当前后端报告中的索引，默认优先硬件高性能适配器，不自动选择软件适配器。两种 API 的枚举顺序可能不同。`--exit-after-ms N` 或 `--frames N` 自动关闭窗口。`--window-test` 在呈现中执行十次尺寸/暂停变化与两次最小化恢复，不要用过短的退出时间截断它。不提供 `--headless` 时才运行窗口循环；`--headless` 单独使用仍只输出设备报告。

验收并保存读回图像：

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --headless --validation on --rhi-test --output out/m1
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --headless --validation on --rhi-test --output out/m1
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --window-test
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --validation on --frames 1
```

`out/m1/dx12` 和 `out/m1/vulkan` 各包含 `triangle-noise.bmp`、`noise.bmp` 与去除行 padding 的 `noise.rgba`。CTest 使用各构建目录的 `acceptance/后端` 输出图像与原始数据，`logs` 输出日志，`Testing/Temporary/LastTest.log` 保留测试结果。Compute 噪声为固定种子、257×131 的两次 Dispatch，CPU 参考检查全部像素；屏幕样例另测深度、绕序、线性混合、sRGB 和采样坐标。

应用从可执行文件同目录的 `shaders/dx12` 或 `shaders/vulkan` 读取构建产物，启动工作目录可以不同。修改 HLSL 后运行正常构建即可重编译，不需要在运行时寻找 DXC。M1 使用固定测试入口；M2 再引入正式 Shader 资产工具链。

日志默认写入当前工作目录的 `logs/renderlab.log`，可用 `--log PATH` 修改。JSON 报告通过 `--report PATH` 指定，父目录会自动创建。日志与报告均为 UTF-8 无 BOM、LF。返回码 0 表示成功，1 表示运行或验证错误，2 表示参数错误。

## 验证结果与边界

M0 的设备初始化历史见 [M0 基础模块](../docs/m0-foundation.md)。M1 的 API、生命周期、分配预算和支持范围见 [最小 RHI](../docs/m1-rhi.md)，各阶段的实际验收配置与结论统一见 [阶段计划](../design/phaseplan.md)。

M1 首期资源为单 mip/layer 的 2D 纹理、显式单流顶点布局和单 Graphics Queue；Vulkan 的 Host 内存要求 coherent，Timestamp 要求 64 位有效计数。资源分配采用显式预算下的 dedicated/committed 分配，尚未采用子分配、Bindless 或 GPU 剔除。验证开启时的 Timestamp 只验证读回链路，不是性能基准。
