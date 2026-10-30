# 构建与运行

RenderLab 支持 Windows x64，使用 C++20、MSVC、Windows SDK、CMake 和 Ninja。Sandbox 在 DX12/Vulkan 上展示三角形与 Compute 噪声纹理，支持独立的设备报告和 RHI 读回验收。

## 工具链

固定功能状态与模板附件的独立 GPU 回归可运行 `ctest --preset windows-debug -R 'render_state|rendergraph.contracts' --output-on-failure`，需配置 `RENDERLAB_GPU_TESTS=ON`；状态与逐面访问接口见 [渲染状态](../docs/render-states.md)。

参数 ABI 生成回归为 `ctest --preset windows-debug -R 'parameters.contracts|shader.contracts' --output-on-failure`。Shader 工具新增 `--cpp OUTPUT.hpp`，为带 Schema 的 `.params` 输出 C++ 数据/打包代码；参数声明、类型头与 handle 见 [参数接口](../docs/shader-parameters.md)。

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

输出分别在 `out/build/windows-debug` 和 `out/build/windows-release`。默认注册 14 项不需要创建设备的测试，包含实际 DXC 双目标编译的 `shader.contracts`、生成 C++ 契约的 `parameters.contracts` 和流式计时数据的 `benchmark.contracts`。预设与依赖版本纳入 Git，本机设置使用被忽略的 `CMakeUserPresets.json`。

完整验收启用 GPU 测试，当前共 58 项（14 项 CPU/CLI、44 项 GPU）。GPU 测试要求验证层和桌面会话，会短暂打开原生窗口：

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

间接提交专项启用 `RENDERLAB_GPU_TESTS=ON` 后构建 `renderlab_indirect_tests`，执行
`ctest --test-dir out/build/windows-debug -R 'gpu\.(dx12|vulkan)\.indirect' --output-on-failure`。
Release 替换构建目录；测试要求支持多条间接、Count 与非零 firstInstance 的设备，始终使用 Validation On。
公共 ABI、同步与覆盖范围见 [GPU Driven](../docs/gpu-driven.md)。

有界非一致纹理数组的独立入口是 `renderlab_descriptor_array_tests`，CTest 使用
`-R 'gpu\.(dx12|vulkan)\.descriptor_array'`；要求 64 个 sampled-texture 数组元素和对应非一致索引能力。

实际场景可添加 `--submission indexed-direct` 或 `--submission cpu-indirect`，与
`--pipeline forward|deferred|raytracing` 独立组合；默认 `--submission direct` 保留原始基线。
例如 `renderlab_sandbox.exe --backend vulkan --headless --validation on --project sample/m4/Pbr.rlproject --submission cpu-indirect --output out/indexed`。
场景专项为 `renderlab_indexed_scene_tests` / `gpu.dx12.indexed_scene` / `gpu.vulkan.indexed_scene`，
支持范围、容量与内存边界见 [GPU Driven](../docs/gpu-driven.md#场景索引基线)。

`--submission gpu-culled` 启用 Compute 视锥剔除与 GPU Count，保留上述 CPU 对照。

场景可独立添加 `--recording-threads 4` 启用 CPU 并行原生命令录制，范围 1..16，默认 1；与几何提交/管线选择独立。资源准备和 Graph 回调仍在 owner 线程，单 Graphics Queue 按原序提交。使用 `--cpu-profile` 和 Benchmark 的 `recordingThreads` 配对测量；`rhiNativeRecordMs` 包含任务调度/等待，已经包含在 NativeSubmit 中。`ctest --test-dir out/build/m5-release -R 'recording.contracts|indexed_scene' --output-on-failure` 检查线程持久性、异常与双后端串并行图像/时间戳；契约见 [并行录制](../docs/parallel-recording.md)。

场景 `--submission gpu-culled --async-compute` 可把清零/视锥剔除交给独立 Compute Queue，默认关闭；可与 `--recording-threads 1|4` 配对。DX12 使用 COMPUTE 队列，Vulkan 首版要求 Graphics family 的第二队列；不支持时明确报错。`ctest --test-dir out/build/m5-release -R 'rendergraph.contracts|indexed_scene' --output-on-failure` 验证依赖、失败保活、GPU 可见列表与连续帧 History。运行 `python tools/benchmark/run.py sample/benchmark/async-compute.json --output out/benchmarks/async-compute` 前先由 indexed_scene 测试生成规模场景；状态与交接见 [异步 Compute](../docs/async-compute.md)。

`--memory-alias` 可选复用同 Graphics Queue 上不重叠的瞬态附件，默认关闭，可与上述队列/线程选项组合。`ctest --test-dir out/build/m5-release -R 'rendergraph.contracts|\.alias$|indexed_scene' --output-on-failure` 验证规划、原生共享分配、取消/连续帧和像素一致性；分配与限制见 [内存 Alias](../docs/memory-alias.md)。
规模性能套件先运行 `ctest --preset windows-release -R 'gpu.dx12.indexed_scene'` 生成固定场景，
再执行 `python tools/benchmark/run.py sample/benchmark/gpu-driven.json --output out/benchmarks/gpu-driven`。
该配置为短测；完整条件与局限见 [测量记录](../docs/performance/m6-gpu-driven.md)。

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

## M4 项目场景

构建 Sandbox 会同步内置库到 exe 附近 shaders/RenderLab；修改 HLSL 后重新构建即可更新，不需要修改 C++ 来触发链接。

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --project sample/m4/RenderLab.rlproject
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --validation on --project sample/m4/RenderLab.rlproject
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --headless --validation on --project sample/m4/RenderLab.rlproject --output out/m4
ctest --test-dir out/build/windows-debug -R 'content.contracts|gpu.*.scene' --output-on-failure
```

项目窗口用 WASD 移动，Q/E 下降/上升；上下方向键绕相机自身右轴俯仰，左右方向键绕父空间上轴转向。失焦时清空按键。无 --project 仍是 Custom Graph Demo。--project --headless 输出一帧及 scene-plan.json；--scene-test 在参考场景上增加行为验收，普通项目无需开启。

### Deferred Pipeline

Deferred 与 Forward 可用同一 Scene 项目切换；窗口内启动 `--debug-panel` 后，在调节面板首项选择 Pipeline。Forward 是默认项。CLI 可在 `--pipeline forward|deferred` 中指定离线输出：

```powershell
cmake --build --preset windows-debug --target renderlab_sandbox
out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --headless --validation on `
  --pipeline deferred --project sample/m4/Pbr.rlproject --width 640 --height 360
out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --headless --validation on `
  --pipeline deferred --project sample/m4/Pbr.rlproject --width 640 --height 360
```

GBuffer 格式、深度重建、透明材质路径和限制见 [Deferred Pipeline](../docs/deferred-pipeline.md)。菜单只列出当前已实现的组合；Path Tracing 与 Forward+ 在各自阶段完成后再加入。

参考项目包含原创 Cube/Sphere glTF、棋盘纹理、项目 Copper Shader/材质、父子对象和两个方向光；只选 UUID 最小的启用光。远处立方体用于剔除验证。数据格式与范围见 [M4 场景](../docs/m4-scene.md)。

```powershell
./out/build/windows-debug/renderlab_shaderc.exe sample/m4/Assets/Copper.shader --include shaders --cache out/m4/shader-cache --schema out/m4/copper-schema.json
```

Assets 与 .meta 纳入版本管理，Library 可以清除重建。不要原地改写命令正在使用的 Upload 数据。资产首次上传可等待，普通帧仍使用现有完成点与帧管理。

### Ray Tracing 组合与 Ray Query

光追 progressive 历史最多缓存四个 View，并按最近使用顺序与 native 预算的 1/8 软字节目标淘汰非活动 View；高分辨率可能只保留一到两个。活动 View 不因软目标被拒绝，最终仍受 native 总限额约束。Headless 报告的 `resourceReuse.rayScene` 提供累计打包/上传次数、缓存 View 数、淘汰次数和 progressive 请求字节数。

场景宿主（Sandbox/Player）默认 tracked allocation 上限为 2048 MiB，Graph 资源缓存以其中 3/4 为上限，给资产、AS、历史和上传留出余量；需要时可用 `--gpu-budget-mib 4096` 显式调整。它统计 Buffer/Texture 的 native 分配，包括 Host 可见上传/读回，不是显卡实时可用显存，也不一次性预留这些内存。独立 RHI Demo 默认仍为 256 MiB。窗口分辨率、在途帧和保留的历史视图都会影响峰值；预算不足时日志给出 requested/used/limit。

窗口尺寸可能被桌面工作区限制，profile 的 width/height 来自 CLI 请求参数，不能单独证明实际客户区尺寸。高分辨率验证需检查实际资源规模（如 `--graph-dump` 的 CameraColor/DisplayColor 字节数），或使用明确尺寸的离屏目标；普通 `--window-test` 还会主动缩小窗口。

支持 AS/Ray Query/TraceRays 的设备可在 Pipeline 菜单选择 **Ray Tracing**，CLI 使用 `--pipeline raytracing`。当前组合以 Deferred 为主体，通过独立 Ray Query Pass 生成方向光阴影可见性，并用 TraceRays 生成一次反射、静止历史累积与独立合成；Shadow 纹理查看项显示此可见性图。方向光的 Shadows 开关控制阴影效果。反射仅支持内置 Lit/Unlit 不透明材质。当前仅支持不透明静态几何，Masked 材质会给出诊断，透明材质仍走 Forward。

```powershell
out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on `
  --project sample/m4/RayTracing.rlproject --pipeline raytracing --debug-panel
out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --headless --validation on --as-test
ctest --test-dir out/build/windows-debug -R "acceleration_structure|ray_query_scene" --output-on-failure
```

`--as-test` 还运行原生 TraceRays 的三角形命中/未命中、索引 PBR 和 PSO/ShaderTable 代际重建检查；成功日志分别列出 Ray Query 与 TraceRays。

Texture 菜单可查看 Reflection (raw sample)、Reflection (history) 与 Reflection (denoised)。Final/HDR 默认开启反射降噪重建，面板 **RT denoise (Final / HDR)** 可随时切换；CLI 用 `--denoise on|off`，不重置累积。降噪查看用 `--view reflection-denoised`，始终显示滤波结果；Raw/History 始终未滤波。粗糙度 ≤0.05 的镜面旁路，2048 spp 后旁路，滤波不回写未滤波历史。

默认时间降噪使用独立单帧信号、材质去调制、稳定命中引导、接收表面/反射视差重投影及三尺度方差引导滤波。面板 **Temporal reprojection (requires denoise)** / CLI `--temporal-denoise on|off` 单独控制；关掉此项可回到原空间滤波基线，`--denoise off` 关闭两者。支持静态场景中的相机运动；场景/材质/光照变化、投影/尺寸变化或相机大幅跳转重置时间历史。反射跟踪是局部平面近似，没有对象运动矢量或通用曲面镜像跟踪，仍可能有高光偏差/拖影。面板 Temporal source 是 CPU 判定可供重投影的连续帧数，不是 GPU 接受像素数或 spp；未滤波 RT history 在移动时仍重置。[接口与限制](../docs/temporal-reflections.md)、[完整运动序列对照](../docs/performance/m5-reflection-stability.md)。

默认时间降噪根据与持续 1/N 累积匹配的局部误差，连续结合静止累积与时间先验；大尺度平滑先随置信度提高而减弱，小尺度仍能处理零星亮点。控制权重限制逐帧变化，替换原先 8–64 spp 的固定交接，减轻亮点先扩散再缩小。仍有反射细节模糊和置信度估计偏差，2048 spp 精确回到未滤波结果。无需新增开关，已运行的程序需重启加载 Shader。[自适应重建对照](../docs/performance/m5-adaptive-reflections.md)、[上一版静止收敛记录](../docs/performance/m5-stationary-reflections.md)。

CLI 对应 `--pipeline raytracing --view reflection-raw` / `reflection-history`；raw 固定单样本且不读旧历史，相机运动会重置累积。Final/history 静止时持续按 1/N 累积；面板的 RT history 显示实际 spp 与已观察到的重置次数，超过 64 后应继续增长。纯光栅对照用 `--pipeline forward` 或 `deferred`。自定义 Shader 缺少 GBuffer 或光追表面实现时，窗口菜单恢复原选项并显示原因；如原始 Copper 场景可继续用 Forward，三种模式对照请用下列 RayTracing 项目。

RayTracing.rlproject 与 M4 项目共用 Assets，只保留不透明地面、球和立方体，适合在三种组合间对照。AS 接口、资源声明、scratch 与在途 TLAS 生命周期见 [光追基础](../docs/raytracing.md)。Path Tracing 按 M8 计划保留，Forward+ 按 M6 计划保留；实现后可加入现有类型化菜单选项表。

## M4.3 PBR 参考场景

Pbr.rlproject 与原场景共用 Assets，独立选择固定材质球场景。上排从左至右是粗糙度 0.08/0.3/0.65/1 的金属，下排是对应非金属；下排第三个含 Normal Map，第四个含 Metallic-Roughness 图，底部小球演示 Unlit HDR。Studio、Normal 与 MR 图是原创程序测试纹理。

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --project sample/m4/Pbr.rlproject
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --validation on --project sample/m4/Pbr.rlproject
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --headless --validation on --project sample/m4/Pbr.rlproject --output out/m4-pbr
ctest --preset windows-debug -R 'pbr|content.contracts|gpu.*.scene' --output-on-failure
```

场景文件可保存 exposure（stops）及 environment 的 image UUID、intensity、rotation（度）。空环境图使用 ambient 均匀 radiance；环境源为 sRGB lat-long 图，尚无 .hdr/.exr 导入。材质使用普通 .material，可指定 RenderLab/Lit 或 RenderLab/Unlit；项目 Copper Shader 继续保留。运行中改材质值会按文件时间重新读取；场景文件修改后需重启项目。WASD/QE/方向键控制不变。

新增 pbr.contracts、gpu.{dx12,vulkan}.pbr 与 gpu.{dx12,vulkan}.pbr_window；输出在各构建树 pbr/后端，包含材质球与单项参考 BMP、Graph JSON、日志和隔离测试资产。测试不会修改 sample。双后端图像对照需相同配置，物理模型与支持限制见 [Forward PBR 契约](../docs/m4-forward-pbr.md)。

## M4.4 阴影与透明场景

Shadow.rlproject 选择带实体、网格裁剪和透明薄片的固定场景，地面用于观察阴影。内置 Lit/Unlit 使用 Surface/Blend/AlphaClip 属性：Opaque、Opaque+AlphaClip、Transparent+Alpha 分别对应不透明、alpha 裁剪和 straight alpha 混合；alpha_cutoff 默认 0.5。方向光组件保存 shadows、shadowResolution、shadowBias、shadowNormalBias；旧场景默认关闭阴影。材质为 v2 并引用 Shader UUID，旧资产迁移见 [Shader 资产](../docs/shader-assets.md)。

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --project sample/m4/Shadow.rlproject
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --validation on --project sample/m4/Shadow.rlproject
ctest --preset windows-debug -R 'surface|content.contracts' --output-on-failure
```

专项参考图与 Graph JSON 在构建树 surface/后端；原始场景不会被测试修改。透明在 HDR 空间混合后统一显示变换，按物体中心从远到近排序，不支持相交面的正确排序。单张阴影、alpha 导入和硬件要求见 [契约](../docs/m4-shadows-alpha.md)。

## M4.5 演示与 Player

`--debug-panel` 打开原生调试面板，也可在场景窗口按 F1 显示/隐藏。面板可切换最终画面/HDR/阴影图/环境图集/BRDF LUT、阴影质量、主光阴影、曝光、冻结/重置相机；改动只作用于当前运行。WASD/QE 和方向键使用有焦点的渲染窗口。

```powershell
./out/build/windows-debug/renderlab_sandbox.exe --backend dx12 --validation on --project sample/m4/Shadow.rlproject --debug-panel --quality medium --width 1280 --height 720
./out/build/windows-debug/renderlab_sandbox.exe --backend vulkan --validation on --project sample/m4/Shadow.rlproject --view shadow --fixed-camera
ctest --preset windows-debug -R 'tools_window|player|surface|rendergraph.contracts|core.contracts' --output-on-failure
```

固定基线用 Release、关闭验证层并保留 profiling。`--benchmark 120` 先预热 5 帧再记录 120 帧，最后额外截图，输出 scene.bmp 和 scene-plan.json；JSON 包含 CPU、逐 Pass GPU、RHI 分配和进程内存。相同命令将 backend 改为 vulkan 可对照。

```powershell
./out/build/windows-release/renderlab_player.exe --backend dx12 --validation off --headless --project sample/m4/Shadow.rlproject --quality medium --width 1280 --height 720 --benchmark 120 --profile out/m4-baseline/shadow-dx12.json --output out/m4-baseline/shadow
```

Player 必须指定 --project，不运行 Sandbox 的 RHI/Graph 验收示例。安装为独立目录后，进入该目录运行同样的项目格式；无需 C# 或源码树，机器仍需当前 MSVC x64 运行库和图形驱动。

```powershell
cmake --install out/build/windows-release --prefix out/player
cd out/player
./renderlab_player.exe --backend dx12 --validation on --project project/Shadow.rlproject --debug-panel
```

安装包含 Player、DXC/PIX DLL、shaders/RenderLab 和示例项目 Assets/.meta/清单；不复制 Library，首次启动会重建。基线与边界见 [性能记录](../docs/performance/m4-rtx5060.md)，接口与计时范围见 [工具契约](../docs/m4-tools.md)。

## Benchmark 与双后端分析

先构建 windows-release。单次按时长运行（预热至少 5 帧且至少 2 秒，再测至少 10 秒）：

```powershell
./out/build/windows-release/renderlab_player.exe --headless --backend dx12 --validation off --project sample/m4/Pbr.rlproject --width 1280 --height 720 --quality medium --benchmark-seconds 10 --warmup-seconds 2 --profile out/benchmarks/single/report.json --samples out/benchmarks/single/frames.jsonl --output out/benchmarks/single/capture
```

报告为 v2 JSON，逐帧原始数据为 JSONL；不指定 profile/samples 时自动放到 OUTPUT/BACKEND。旧 `--benchmark N` 继续支持并使用同一数据导出链路。计时与 FPS 含义见 [Benchmark 契约](../docs/benchmark.md)。

批量样例按 DX12/Vulkan 交替顺序分别执行 PBR，默认各 3 次，每次预热 2 秒、测量 10 秒。工具默认自动创建 out/benchmarks/UTC时间戳；指定 `--output DIR` 时要求目录尚不存在。核心运行/分析只需 Python 3.10+，可选图表使用已验证的 Python 3.12 依赖环境：

```powershell
python -m venv out/benchmark-env
./out/benchmark-env/Scripts/python.exe -m pip install -r tools/benchmark/requirements.txt
./out/benchmark-env/Scripts/python.exe tools/benchmark/run.py sample/benchmark/pbr.json --plots
```

可用 `--seconds`、`--warmup-seconds`、`--repeats`、`--backends`、`--validation on|off` 或 `--exe PATH` 覆盖样例；新增场景在配置 cases 中登记，相对项目路径以配置文件目录为基准。完整参数可运行 `run.py --help`。

扩大样本或定位 CPU/驱动差距：

```powershell
python tools/benchmark/run.py sample/benchmark/pbr.json --seconds 20 --warmup-seconds 5 --repeats 8
python tools/benchmark/run.py sample/benchmark/pbr-diagnostics.json
```

诊断配置开启 `cpuProfiling`，也可用 CLI `--cpu-profile`/`--no-cpu-profile` 覆盖；直接 Player 对应 `--cpu-profile`。这组数据额外包含 RHI 创建、提交、Fence 与回收区间/调用计数，计时定义见 Benchmark 文档。

每组输出 manifest.json，各运行目录含原始样本、report/device JSON、日志与测量外截图；analysis 目录含逐指标 CSV、分析 JSON、Markdown 报告及可选 PNG/SVG 图表。报告给出每次 P50/P95/P99、各后端重复均值与重复间标准差。示例独立分析已有数据（替换路径为实际组目录）：

```powershell
./out/benchmark-env/Scripts/python.exe tools/benchmark/analyze.py out/benchmarks/RUN/manifest.json --output out/benchmarks/RUN/reanalysis --plots
python -m unittest discover -s tools/benchmark -p "test_*.py"
```

分析可接受多个完成 manifest 或单次 v2 report，支持独立重画图/新增统计。比较器拒绝截断、失败或配置/设备不匹配的结果；它不会把 Debug/Validation 测试标为正式性能基线。当前为逐帧 GPU 完成点等待的 headless 测量，不代表窗口 Present 的流水吞吐。样例源码按 src/samples/common、rhi、graph、material、scene 分类；一次性迁移脚本已清理，tools/benchmark 为正式维护工具。

## Avalonia 嵌入原型

IDE 打开根目录 `RenderLab.slnx`，应用项目位于 `src/editor/managed/RenderLab.Editor`；它负责托管项目导航和构建，C++ 依赖仍由 CMake 构建。根目录 `global.json` 锁定 SDK，`Directory.Build.props` 统一规则并将各项目产物分别放到 `out/dotnet/<项目名>/{bin,obj}`。首次运行先用下面的构建脚本准备原生 DLL/Shader，再由 IDE 启动 Editor；IDE 构建不会隐式配置或编译 CMake。自定义本机构建树时给 MSBuild 传 `-p:NativeBuildDir=绝对路径`，或将同名环境变量设为该绝对路径。

额外需要 .NET SDK 10.0.401（根目录 global.json 允许同 feature band 的更新补丁）和 PowerShell 7。Avalonia/Fonts.Inter 固定 12.0.5，CommunityToolkit.Mvvm 8.4.1、Microsoft.Extensions.DependencyInjection 10.0.0；Debug 开发工具使用模板的 AvaloniaUI.DiagnosticsSupport 2.2.3。包图由 `src/editor/managed/RenderLab.Editor/packages.lock.json` 锁定。宿主作为后续 Editor 的 MVVM/DI 骨架复用，目前只支持 Windows x64，属于 M7.1 的提前集成试验；模块职责、生命周期和未验证边界见 [宿主契约](../docs/editor-host.md)。

当前工程已从官方 12.0.5 MVVM 模板的 CommunityToolkit 分支整理，不需要重新生成。要在独立目录重现参考模板，可执行以下命令；不要向现有宿主目录使用 `--force` 覆盖 Bridge、manifest 或生命周期实现。

```powershell
dotnet new install Avalonia.Templates::12.0.5
dotnet new avalonia.mvvm -n EditorReference -o out/editor-reference -f net10.0 `
    -m CommunityToolkit -av 12.0.5 --remove-view-locator --no-restore
```

在 x64 Developer PowerShell 中配置原生 Release 构建，然后运行脚本。脚本构建 Bridge、按锁文件恢复托管依赖，并将 DLL、运行依赖和 Shader 复制到托管输出目录。使用已有 Ninja/NMake 构建树时通过 `-NativeBuildDir` 指定目录，Configuration 应与原生构建树匹配。

```powershell
cmake --preset windows-release
./tools/build-editor.ps1
./out/dotnet/RenderLab.Editor/bin/Release/net10.0/RenderLab.Editor.exe --backend dx12
./out/dotnet/RenderLab.Editor/bin/Release/net10.0/RenderLab.Editor.exe --backend vulkan
```

默认目标帧率为 60，`--target-fps 1..240` 设置上限并保留 VSync；`--target-fps 0` 取消限速与强制 VSync。驱动/合成器和实际 CPU/GPU 吞吐仍可能限制帧率，静态光栅仍按需重绘。光栅视口静止后显示 Idle，输入、曝光、管线或尺寸变化后重绘；光追在达到 Renderer 的样本计数上限前继续刷新。`--validation on|off` 和 `--profile on|off` 控制设备验证与 GPU Pass 查询，Release 默认关闭验证，Debug/默认 smoke 开启验证。

固定相机窗口基准显式使用连续刷新；先预热，再采集原生渲染线程的逐帧完成间隔，输出 `performance.json` 和 `frames.json`，不在每帧写文件：

```powershell
./out/dotnet/RenderLab.Editor/bin/Release/net10.0/RenderLab.Editor.exe `
    --backend dx12 --pipeline deferred --validation off --profile on `
    --target-fps 60 --warmup-seconds 2 --benchmark-seconds 5 `
    --output out/m7/benchmark-dx12
```

比较后端/验证模式时保持项目、管线、像素尺寸、缩放、目标帧率与预热一致。检查报告的 `DroppedSamples` 和关闭报告；有丢样的结果不能作为完整分布，GPU Pass 时间也不代表扫描输出延迟。[调度测量](../docs/performance/editor-scheduling.md) 保留本轮基线和解释边界。

有完整 NuGet 缓存的离线机器可以增加 `-OfflinePackageCache C:/你的目录/.nuget/packages`；恢复仍使用 locked mode。直接运行 dotnet 时在仓库根目录使用 `RenderLab.slnx` 作为构建入口；需要自定义原生路径时使用 `-p:NativeBuildDir=绝对路径`。构建脚本跳过本原型没有使用的 Avalonia 商业产品构建遥测，不修改用户全局设置。

默认加载源码仓库的 `sample/m4/RayTracing.rlproject`，初始为 Deferred。`--project` 可以指定其他项目，`--shaders` 可指定引擎 Shader 根目录；默认使用 exe 同目录的 shaders。点击原生视口后使用 WASD/QE、方向键或右键拖动。此宿主未打包示例项目，移出仓库运行时必须显式提供项目路径。

M7.2a 接入 Dock.Avalonia 12.0.0.2 与 Serilog：首次启动只显示 Scene 与 Render Settings，Window 菜单打开 Log、基础 Profiler 与 Render Graph 骨架；完整 Graph/Profiler 诊断分别属于后续子阶段。Layout 提供 Default、Diagnostics 与恢复保存布局；普通运行将布局写到项目旁的 UserSettings/Editor/layout.json，自检/基准使用各自输出目录，不覆盖日常布局。Scene 浮动/停回会安全重建原生视口并保留相机与管线/曝光。Log 支持筛选、搜索、复制、清空显示和打开日志目录；关闭面板仍写输出目录 logs 下的滚动 JSONL，原生日志保留独立文件。相关包与测试依赖均进入锁文件。

M7.2b 的 Render Settings 已接入提交方式、录制线程/异步/Alias、已有调试视图、光照/环境、阴影、反射、导航速度、帧率/连续刷新和 GPU profiling。面板显示 Pending/Applied/Rejected 及配置 ID；暂停后可继续改参数，但须恢复或点击 Single frame 才确认生效。Defaults 恢复本次载入默认值，Save/Load preset 使用 UserSettings/Editor/render-preset.json，不改 Scene/Material。Scene 浮动/重建保留整份运行配置。图形 API 与 Validation 仍在启动时选择，动态 API 切换属于 M7.2c。

配置验收沿用下方 --smoke：扩展覆盖受支持的三管线/四提交、能力拒绝、快速请求合并、暂停/单帧、预设/宿主重建保留、目标帧率 0↔60，以及曝光保留/照明失效历史。检查实际挂载的 Settings/Log，并通过菜单恢复保存布局，确认 Scene、设置控件和运行配置仍有效。报告保存参与检查的源文件 hash，GPU 专项 gpu.dx12.indexed_scene / gpu.vulkan.indexed_scene 还验证运行覆盖改变真实像素、移除覆盖恢复原图。

双后端自检应分别运行。WinExe 在终端的启动/等待语义可能不同，需要可靠退出码时使用 `Start-Process -Wait -PassThru`；下面的隐藏启动用于无人值守自检，手动检查界面直接运行上面的命令。

```powershell
foreach ($backend in @('dx12', 'vulkan'))
{
    $hostTest = Start-Process ./out/dotnet/RenderLab.Editor/bin/Release/net10.0/RenderLab.Editor.exe `
        -ArgumentList "--backend $backend --smoke --output out/m7/smoke/$backend" `
        -WindowStyle Hidden -Wait -PassThru
    if ($hostTest.ExitCode -ne 0) { throw "Editor host smoke failed: $backend" }
}
```

成功输出 `smoke.json` 和 `close-reports.json`，失败输出 `smoke-failure.txt` 或 `managed-error.txt`。日志/测试报告默认写入仓库的 `out/m7/<backend>`；可用 `--output` 设置独立目录，避免旧报告混淆。本次实测使用 `out/build/m5-release` 原生构建树，原始报告在 `out/m7/嵌入验证/dx12-manifest` 与 `out/m7/嵌入验证/vulkan`。

MVVM/DI 修订后的双后端回归还验证实际编译绑定、按钮命令、错误回滚和重建后的配置保留，最终报告使用 `out/m7/mvvm-final/<backend>`。相机方向有独立 CPU 回归：

```powershell
cmake --build --preset windows-release --target renderlab_editor_input_tests
ctest --preset windows-release -R '^editor.input$' --output-on-failure
```

## 验证结果与边界

M0 的设备初始化历史见 [M0 基础模块](../docs/m0-foundation.md)。M1 的 API、生命周期、分配预算和支持范围见 [最小 RHI](../docs/m1-rhi.md)，各阶段的实际验收配置与结论统一见 [阶段计划](../design/phaseplan.md)。

M1 首期资源为单 mip/layer 的 2D 纹理、显式单流顶点布局和单 Graphics Queue；Vulkan 的 Host 内存要求 coherent，Timestamp 要求 64 位有效计数。资源分配采用显式预算下的 dedicated/committed 分配，尚未采用子分配、Bindless 或 GPU 剔除。验证开启时的 Timestamp 只验证读回链路，不是性能基准。
