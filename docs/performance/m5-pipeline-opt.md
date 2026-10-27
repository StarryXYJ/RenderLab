# M5 光追管线优化

日期：2026-10-27。设备 RTX 5060 / NVIDIA 591.74，MSVC Release。原始本机报告、逐帧 JSONL、图像和隔离资产位于 `out/rt-pipeline-opt/`。

## 静态场景 CPU 准备

对照程序为 `c99d023` 的 CastShadows 修复构建，保存在 `out/rt-pipeline-opt/before/`。测试将隔离 Sphere 资产替换为 512×256 UV 球：131841 顶点、786432 个 uint32 索引；其他场景对象和材质不变。这个场景用于放大按顶点数量增长的准备成本，不代表原始小场景的收益。

两版本均以 640×360、Ray Tracing、关闭显示降噪、validation off 运行；各预热 64 帧、测量 256 帧，按 DX12/Vulkan 交替各三次。CPU Render 包含场景准备、Graph 和提交，不含宿主随后等待 GPU 的时间；串行帧不是窗口流水 FPS。优化没有修改采样、BRDF 或纹理表示。

| 后端 | 之前 CPU Render，三次 ms | 缓存后 CPU Render，三次 ms | 三次均值 |
| --- | --- | --- | --- |
| DX12 | 8.212 / 6.430 / 7.115 | 1.214 / 1.032 / .983 | 约 7.25 → 1.08 ms |
| Vulkan | 6.638 / 6.336 / 7.289 | .867 / .833 / .841 | 约 6.75 → .85 ms |

六组优化后报告均为一次 geometry pack、一次 surface upload batch；预热后未重打包/上传。相同后端、相同运行序号的六对最终 BMP 均逐字节相同。GPU 算法保持不变；操作系统负载和频率影响短测数字，未把 CPU 降幅推广为任意场景的帧率提升。

Release 双后端反射完整回归通过，新增合约覆盖静止/相机运动零重复工作、材质编辑一次合批和 CastShadows 编辑零 surface 上传。586 张完整回归 BMP 与对照哈希相同，validation errors=0。

实现边界：缓存当前选择的不可变导入网格，不累计所有历史选择；实例/材质表仍按帧生成并比较，纹理文件版本仍检查。内容变化将所有变化表合并为一次同步上传，尚无异步上传、动态几何或 BLAS compaction。

复现：运行 `out/rt-pipeline-opt/prepare-large.ps1` 生成隔离资产；对照与当前 Player 分别执行下列参数，依次替换后端、输出目录并重复三次：

```powershell
out/build/m5-release/renderlab_player.exe --headless --backend dx12 --validation off `
  --project out/rt-pipeline-opt/large/RayTracing.rlproject --pipeline raytracing `
  --denoise off --width 640 --height 360 --warmup-frames 64 --benchmark 256 `
  --output out/rt-pipeline-opt/reproduce --profile out/rt-pipeline-opt/reproduce/dx12.json
```

运行合约与统计入口见 [光追文档](../raytracing.md)。各子阶段实际回归结果由 [阶段计划](../../design/phaseplan.md) 维护。
