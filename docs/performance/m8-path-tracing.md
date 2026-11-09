# 路径追踪验证记录（进行中）

运行入口见 [手册](../../manual/build.md)，阶段状态见
[阶段计划](../../design/phaseplan.md#m8-专题深化与作品交付)。

## BSDF 数值检查

RTX 5060 上 Release 的 DX12/Vulkan `gpu.*.acceleration_structure` 验收分别运行 6 组
金属度、粗糙度和视角。GPU 每组以 65536 个样本对混合采样的 `BRDF*cos/pdf` 求均值；
CPU 独立以 512×512 中点网格积分半球上的同一 BRDF。逐通道允许绝对误差
`0.025 + 0.04 * 参考值`，并拒绝非有限或负权重。两后端通过；例如 DX12 第一组红色通道
GPU 0.696917、CPU 0.693426。测试覆盖有限组合与有限积分精度，不证明所有材质、路径或场景无偏。
日志位于 `out/build/m5-release/logs/{dx12,vulkan}-acceleration-structure.log`。

场景回归覆盖独立 Trace/Accumulate 图、连续累积，及相机、场景、材质、光照、分辨率、
曝光变化的历史行为。开发中首次反射回归因历史 ID 修改失败，恢复反射 ID 后复验通过；
新增路径测试首次因共用渲染器改变原测试 TLAS 初始条件失败，隔离测试渲染器后复验通过。
最终 Debug/Release 的 DX12/Vulkan `gpu.*.reflections` 与 `gpu.*.acceleration_structure`
各配置 4/4。GPU validation errors=0 不代表无既有验证警告。

## 固定场景收敛观察

2026 年 11 月 9 日，RTX 5060、DX12、Release、validation 开启，`RayTracing.rlproject`，
固定相机、160×90、无预热；分别测量 16、64、256、4096 帧。Benchmark 报告最后测量帧为
对应 spp，**保存图片另渲染一帧**，因此图片分别为 17、65、257、4097 spp。以 4097 spp
同实现图片为参考，按最终 sRGB 8 位 RGB 通道计算：

| 图片 spp | MSE（8 位值平方） | MAE（8 位值） |
| --- | ---: | ---: |
| 17 | 39.2418 | 2.2599 |
| 65 | 24.3822 | 1.4295 |
| 257 | 11.4845 | 0.8474 |

对照表明此场景随样本数增长趋向该高样本结果，不是独立真值、HDR 误差、多个随机种子或
其他场景的收敛证明。固定路径长度、偏移、法线贴图、有限图集和采样方差仍会产生误差。
验证层错误为零；64/256/4096 运行关闭时原生分配及保留账本为零。证据位于
`out/m8-pbr-convergence/{16,64,256,4096}` 和 `out/m8-pbr-{64,256,4096}.log`。

用 `python tools/compare_bmp.py <参考.bmp> <待比较.bmp>...` 复算最终图像的 RGB 8 位
MSE/MAE；工具只接受未压缩的 24/32 位 BMP，不把色调映射后的误差当作线性 HDR 误差。

同条件另测 `PathTracingGallery.rlproject`：25 个网格实例加地面，包含两种既有材质；
65 和 513 spp 的截图相对同实现 4097 spp 图的 MSE 分别为 30.7461、10.3357。DX12 与
Vulkan 的 513 spp 图之间 MSE 为 0.000208。两后端 validation errors=0，关闭时原生分配和
保留账本为零。DX12 512 帧测量的 Trace 均值 0.033 ms、原生分配峰值 10.7 MiB；Vulkan
分别为 0.028 ms、10.1 MiB。CPU 渲染均值分别为 1.517、2.123 ms；无预热，包含首次场景与
程序准备，不应解释为稳定帧或 GPU 吞吐。样本数、相机、资产和环境仍有限，不代表其他硬件。
证据位于 `out/m8-gallery`、`out/m8-gallery-*.log`；数值不可与包含预热的运行直接比较。

## 两种规模的短测

同一 RTX 5060、Release、validation 关闭，headless 串行等待；每组一次运行，预热 32 帧、
测量 128 帧。CPU 是宿主观测的 Render 提交均值，GPU 是完成的 Trace Pass 均值，内存为
tracked native allocated 峰值，不含完整驱动/进程内存。数值单位分别为 ms、ms、MiB。

| 场景 | 分辨率 | DX12 CPU / Trace / 峰值 | Vulkan CPU / Trace / 峰值 |
| --- | --- | --- | --- |
| RayTracing（3 实例） | 320×180 | 0.220 / 0.036 / 10.9 | 0.181 / 0.031 / 10.8 |
| RayTracing（3 实例） | 640×360 | 0.206 / 0.080 / 22.4 | 0.192 / 0.069 / 21.5 |
| Gallery（26 实例） | 320×180 | 0.260 / 0.041 / 10.9 | 0.240 / 0.036 / 10.8 |
| Gallery（26 实例） | 640×360 | 0.267 / 0.097 / 22.4 | 0.253 / 0.085 / 21.5 |

短测、单设备和低分辨率不代表窗口 FPS、其他 GPU 或可重复的后端排名；场景共享资产与
环境，不能代表复杂生产内容。逐帧与报告位于 `out/m8-measure/<后端>-<场景>-<宽度>`，
对应控制台日志为 `out/m8-measure-*.log`。

## GPU Capture

使用 RenderDoc 1.47 便携版对 Release/DX12 的安装包、Gallery、640×360 窗口第 10 帧采集
`out/m8-path-gallery.rdc`（10,084,060 字节，SHA-256
`4e75623b06d67df078578be65d856555e86b5e7ae06d4e814b65ec9918d8a3a3`）。官方
`renderdoccmd replay --loops 1` 本机退出码为 0；导出结构化 XML 可见 `PathTracing.Trace`、
`DispatchRays`、`PathTracing.Accumulate`、Dispatch 与 DrawInstanced，缩略图可读取。
自编离线检查辅助程序在 OpenCapture 时崩溃，其原因尚未定位；不能据此声称已逐事件验证
资源或 Shader。未采集 Vulkan Capture。工具、结构化导出和日志在 `out/tools/renderdoc-1.47`、
`out/m8-path-gallery.xml` 与 `out/m8-renderdoc-replay.log`，捕获时 validation 关闭。

## 运行包与录像

首次完成版本的 Release 安装产物保存在 `out/RenderLab-PathTracing-demo-baseline.zip`（10,893,913 字节，SHA-256
`50304a37862166e19f35c306ff5112395a9ffddfd5167a05acf6e202e5b69f5e`）。解压到独立目录后，
从包内使用 Gallery 项目运行 DX12/Vulkan、160×90、8 帧，validation errors 和关闭账本为零；
运行使用包内可执行文件、Shader 与项目文件，首次重建 Library。未在第二台机器实测。

`out/RenderLab-PathTracing-window.mp4` 是该安装包 DX12 窗口 640×360 的 8 秒实际录屏，
固定相机，录制开始时已有累积，用户无相机交互；录像使用初版、尚未启用后续显示降噪。
最终窗口运行 1767 帧，validation errors
和关闭账本为零。另有 `out/RenderLab-PathTracing-progress.mp4`，它由离线保存的
17、65、257、2049 spp 图像拼成，**不是实时录屏**。两者是本机生成的交付文件，不由 Git
追踪；重新生成需要相应输出及编码工具。
