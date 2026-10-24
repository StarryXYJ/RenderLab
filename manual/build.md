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

配置时由 cmake/MsvcIncludes.cmake 调用当前 MSVC，按 UTF-8 读取实际 /showIncludes 输出并设置 Ninja 的依赖前缀，不依赖旧缓存中的本地化编码。检测失败会中止配置，避免悄悄丢失头文件依赖。

旧构建目录可能已经混入不同头文件版本的目标文件。头文件中的 Command variant 等共享类型变化后，这会表现为 shared_ptr 析构访问冲突，即使增量构建报告成功。修正检测后必须完整重编译一次：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --clean-first
```

不要手工修改生成的 Ninja 文件，也不要仅重新链接 exe。以后配置仍需使用同一开发环境；更换编译器时用 --fresh 重新检测工具链，并完整重编译。

## 依赖

| 依赖 | 固定 Git 对象 | 用途 |
| --- | --- | --- |
| Vulkan-Headers | `2cd90f9d20df57eac214c148f3aed885372ddcfe` | Vulkan 头文件 |
| volk | `a8da8ef3368482b0ee9b0ec0c6079a16a89c6924` | 从系统 Vulkan Loader 加载函数 |
| WinPixEventRuntime | NuGet `1.0.240308001`，固定 SHA-256 见 `cmake/Dependencies.cmake` | DX12 GPU Marker |
| SPIRV-Reflect | `e55086b044225f9b511ae44dbf9b079d3625943f` | SPIR-V 绑定与常量反射 |
| nlohmann/json | `65ee68451d8eb2b5f3a30b410476ab83deb3289b`（v3.12.0） | 缓存、Schema 与材质序列化 |

Vulkan-Headers、volk 和 SPIRV-Reflect 对应 Vulkan SDK 1.4.321.0 源码版本，实际运行最低要求 Vulkan 1.3。首次配置需要 Git 和 GitHub 网络访问。依赖来源与授权说明位于各依赖源码的 LICENSE 文件，不在引擎源码中复制或修改它们。

构建需要能同时输出 DXIL 和 SPIR-V 的 DXC。CMake 从 PATH、`VULKAN_SDK/Bin` 或显式 `RENDERLAB_DXC` 路径查找，不在共享配置中保存本机 SDK 路径。M2 同时需要 `dxcapi.h`、`dxcompiler.lib` 和 `dxcompiler.dll`；默认在 DXC 相邻的 Include/dxc、Lib 和 Bin 中定位，也可分别指定 `RENDERLAB_DXC_INCLUDE`、`RENDERLAB_DXC_LIBRARY`。运行库 DLL 随工具复制到输出目录。完整 Vulkan SDK 不是构建必需，但提供 DXC 与验证层更方便。运行需要显卡驱动提供的 Vulkan Loader；DX12 使用系统运行库和 Windows SDK。严格验证额外需要 Windows Graphics Tools 的 D3D12 Debug Layer，以及 Khronos Vulkan Validation Layer（含 synchronization validation 设置）。

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

输出分别在 `out/build/windows-debug` 和 `out/build/windows-release`。默认注册 7 项不需要创建设备的测试，包含实际 DXC 双目标编译的 `shader.contracts`。预设与依赖版本纳入 Git，本机设置使用被忽略的 `CMakeUserPresets.json`。

完整验收启用 GPU 测试，共 19 项。GPU 测试要求验证层和桌面会话，会短暂打开原生窗口：

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

Sandbox 从可执行文件同目录的 `shaders/dx12` 或 `shaders/vulkan` 读取 M1 构建产物，启动工作目录可以不同。修改 M1 HLSL 后运行正常构建即可重编译；该底层样例保留固定入口，独立于 M2 资产工具链。

日志默认写入当前工作目录的 `logs/renderlab.log`，可用 `--log PATH` 修改。JSON 报告通过 `--report PATH` 指定，父目录会自动创建。日志与报告均为 UTF-8 无 BOM、LF。返回码 0 表示成功，1 表示运行或验证错误，2 表示参数错误。

## Shader 与材质工具

```powershell
./out/build/windows-debug/renderlab_shaderc.exe shaders/m2/surface.shader --cache out/shader-cache --schema out/surface-schema.json
./out/build/windows-debug/renderlab_shaderc.exe shaders/m2/surface.shader --target spirv --schema out/spirv-schema.json

./out/build/windows-debug/renderlab_material_demo.exe dx12 shaders/m2/surface.shader out/m2/dx12
./out/build/windows-debug/renderlab_material_demo.exe vulkan shaders/m2/surface.shader out/m2/vulkan

ctest --preset windows-debug -R 'shader.contracts|material' --output-on-failure
```

shaderc 默认编译两个目标，支持 `--target dxil|spirv|both`，省略 `--schema` 时向终端输出完整参数表、元数据、偏移和变体。开启 `--cache` 后，相同输入的第二次编译输出 cacheHit=true；修改声明、工具 Include、宏或共享模块会失效。清除缓存可直接重建。

material_demo 是代码驱动的无窗口材质样例和 GPU 验收入口，始终启用严格验证。它创建材质、修改颜色/纹理/变体、保存与重载，渲染到 sRGB 目标并读回；还测试 Compute cbuffer/SRV/UAV 和后台重载。输出目录包含 `material-default.bmp`、`material-edited.bmp`、`material-reloaded.bmp`、`sample.material`、`material.log` 和可重建缓存。测试只修改输出目录中的 Shader 副本。源码语法与 API 见 [Shader 与材质](../docs/m2-shader-material.md)。

若已有 Sandbox 实例占用输出 exe，可使用另一个构建目录：

```powershell
cmake -S . -B out/build/windows-m2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DRENDERLAB_GPU_TESTS=ON
cmake --build out/build/windows-m2-debug --parallel 4
ctest --test-dir out/build/windows-m2-debug --output-on-failure
```

仍须在 Developer PowerShell 中配置 DXC 环境。M2 本轮使用上述隔离 Debug 目录及相同方式配置的 `windows-m2-release`，避免覆盖已运行的 Sandbox。

## Render Graph 与 Feature

默认 Sandbox 窗口使用 Custom Feature 注册 → Create → 每帧 Graph 构图，不注入 Forward 内置流程。`--rhi-test` 保留直接 RHI 验收，`--graph-test` 验证图路径与直接路径的图像一致性。

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --graph-dump out/m3/frame.json
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --validation on --graph-no-update
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --validation on --graph-no-cull --frames 60

./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --headless --validation on --graph-test --output out/m3 --log out/m3/dx12.log
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --headless --validation on --graph-test --output out/m3 --log out/m3/vulkan.log
ctest --test-dir out/build/windows-debug -R 'rendergraph.contracts|gpu.*.graph' --output-on-failure
```

示例使用标准 windows-debug 预设，也可使用已有隔离构建目录或 windows-release。M3 完整测试为 19 项：7 项 CPU/CLI 和 12 项 GPU。`--graph-no-update` 省略噪声更新 Pass，画面显示未经过 XOR 更新的噪声；`--graph-no-cull` 让无消费者的测试 Feature 也执行。两者用于确认功能与图编译选择，不改变执行器。

`--graph-dump` 导出首个窗口帧的计划，包含 Pass 来源、存活/剔除原因、资源版本、依赖、屏障、生命周期和 CPU 编译/准备/录制耗时。GPU Capture 可以通过每个 Pass 的 Marker 定位工作；当前不提供每 Pass GPU Timestamp。

`--graph-test` 输出 `后端/triangle-noise.bmp`、`noise.bmp`、`noise.rgba`、六帧的 `plan-N.json` 、`buffer-plan.json` 和 `forward-plan.json`。正常更新时，全部颜色字节与直接 RHI 绘制比较；固定噪声保持既有 SHA-256。缓冲探针验证上传、复制、常量、结构化缓冲的两次 Compute 和读回。普通 Forward 另检查透明混合、只读深度与独立阶段快照。CPU 合约检查 Create 的乱序注册、依赖缺失/重名/环和已创建组合隔离。详细 API 与限制见 [Render Graph 与 Feature](../docs/m3-rendergraph.md)。

## 验证结果与边界

M0 的设备初始化历史见 [M0 基础模块](../docs/m0-foundation.md)。M1 的 API、生命周期、分配预算和支持范围见 [最小 RHI](../docs/m1-rhi.md)，各阶段的实际验收配置与结论统一见 [阶段计划](../design/phaseplan.md)。

M1 首期资源为单 mip/layer 的 2D 纹理、显式单流顶点布局和单 Graphics Queue；Vulkan 的 Host 内存要求 coherent，Timestamp 要求 64 位有效计数。资源分配采用显式预算下的 dedicated/committed 分配，尚未采用子分配、Bindless 或 GPU 剔除。验证开启时的 Timestamp 只验证读回链路，不是性能基准。
