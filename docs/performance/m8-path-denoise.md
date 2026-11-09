# 路径追踪显示降噪验证

## 方法与边界

`PathTracing.Denoise` 只读取未滤波的 progressive HDR 均值和当帧主射线的法线、距离、
基础色与粗糙度引导。它在相近表面/材质的 3×3 邻域内用亮度中位数限制异常值并做空间
加权，结果仅用于显示，绝不写回累积历史。背景和 Unlit 旁路；强度在 256 spp 后淡出，
2048 spp 起精确旁路。由于使用当前帧抖动的引导，没有运动矢量或时间重投影，边缘可能
闪动；限制亮度和空间平均均有偏，可能压低真实高光、模糊反射或细节。它不是无偏估计器，
也不是通用实时去噪器。编辑器 Ray tracing → Spatial denoise 或命令行 `--denoise off`
可关闭，保留原始结果。

每帧生成两张 RGBA32F 引导图，即使关闭滤波也会产生；启用时再生成一张 RGBA32F 显示图。
这是额外的显存与带宽成本，尤其在高分辨率；未验证 4K、其他 GPU 或长期交互。

## 固定场景对照

2026 年 11 月 9 日，RTX 5060、Release、DX12、validation 开启，固定相机与种子；测量使用
显示滤波候选版本，之后仅修复了程序初始化失败时的原子发布，未更改滤波或采样算法。测量帧数
64 或 256，无预热，程序在测量后额外渲染一帧截图。以同一积分器的高样本、滤波关闭截图
为参照，计算最终 sRGB RGB 8 位误差（MSE 单位为 8 位数值平方）：

| 场景与截图样本 | 参考 | 原始 MSE / MAE | 显示滤波 MSE / MAE |
| --- | --- | --- | --- |
| Gallery，640×360，65 spp | 2049 spp | 27.3864 / 1.3852 | 15.2795 / 1.0351 |
| RayTracing，160×90，65 spp | 4097 spp | 24.3822 / 1.4295 | 14.8151 / 1.0535 |
| RayTracing，160×90，257 spp | 4097 spp | 11.4845 / 0.8474 | 11.0873 / 0.8270 |

Gallery 的启发式孤立亮点计数（sRGB 加权亮度大于 120 且高于 3×3 中位数 50，包含可能的
真实高光）从 468 降至 5；该指标不是白点成因或视觉质量的完整判断。滤波关闭的 65 spp
图与改动前原始图 hash 相同；2049 spp 滤波开/关及改动前原始图三者 hash 相同。双后端
Gallery 65 spp 滤波图之间 MSE 为 0.000068；Vulkan 的 2049 spp 滤波开/关也 hash 相同。
最终初始化修正后的 DX12 65 spp 滤波开/关图分别与上述候选图 hash 一致。
上述高样本图仍有方差，不是独立真值；
单一固定种子、有限场景和显示空间指标不能推断所有场景或高光无损。

Gallery 640×360 的本次短测，DX12/Vulkan 滤波 Pass 均值 0.031/0.029 ms，validation
开启，未做性能预热；DX12 跟踪分配峰值滤波开/关分别 33.6/29.9 MiB。四组运行关闭时
native 账本与 validation errors 为零。记录在 `out/m8-denoise` 与 `out/m8-denoise-*.log`；
旧版高样本参照见 `out/m8-demo-frames/2048`、`out/m8-pbr-convergence/4096`。误差可用
`python tools/compare_bmp.py --outliers <参考.bmp> <比较.bmp>...` 复算，不代表稳定窗口帧率。

更新后的本机演示包 `out/RenderLab-PathTracing-demo.zip` 为 10,897,800 字节，SHA-256
`deb6eaed22189cf4be6f941812e9b5e1ce6371f7a5d16e16fb51bb6f9e10c234`；解压到
`out/m8-denoise-final-unpacked` 后 DX12/Vulkan 各运行 8 帧，验证错误和关闭账本为零。旧版
RenderDoc Capture 和录像仍对应未滤波初版，不冒充本次结果。
