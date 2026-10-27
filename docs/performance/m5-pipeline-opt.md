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

## 引导图、历史缓存与高 spp 旁路

命中距离的原始 RGBA32F 只使用 x；改为 R32F 保留全部 float32 信息，每分配节省 12 字节/像素。Raw/History、关闭降噪和静止 2048 spp 及以上使用不写引导的 RayGen。高 spp 直接选择 progressive 纹理，省去 Prepare/Temporal/三尺度/Resolve；非活动时间引导在成功提交后释放 CPU 引用，命令仍保护在途版本。原始均值、HDR 时间矩和世界位置保持 FP32。

同一内存探针源码链接优化前 Renderer 库快照和当前库，显式目标 2560×1440，两个在途槽、连续 32 帧，128² 阴影、默认降噪、validation on、原生预算 2048 MiB。对照使用保存的旧 Shader 目录；当前使用本次构建目录。源码、响应文件与 JSON 位于 `out/rt-pipeline-opt/memory_probe.cpp`、`*-probe.rsp` 和 `memory-{before,after}-{backend}/`。探针核对逐帧 spp、实际资源规模、退出账本和 validation。

| 后端 | 之前 native 峰值 MiB | 当前 native 峰值 MiB | 减少 MiB |
| --- | ---: | ---: | ---: |
| DX12 | 1818.69 | 1728.75 | 89.94 |
| Vulkan | 1817.75 | 1727.75 | 90.00 |

两后端 validation errors=0，退出 ledger=0。账本包含 Host 可见内存等 native 分配，不是驱动实时 VRAM 占用；相同算法的分配退休时机也会影响峰值。没有提高预算或降低输出尺寸。

历史缓存另以 1920×1080、三个相同姿态的新 View、每 View 两帧验证。256 MiB 的 progressive 软目标触发一次 LRU 淘汰，两后端保留两个 View，CPU-owned progressive 请求字节数为 199065600（189.84 MiB）；验证错误为零、退出归零。该统计不含时间引导、对齐和已淘汰但在途的分配。常规场景回归另检查四 View 容量溢出、保留最近触及 View、最旧 View 返回冷启动。

高 spp 测量使用 1280×720、原 RayTracing 场景、512² 阴影（场景设置）、validation off，预热 2048 帧后测量 128 帧（2049..2176 spp），最后额外截图在 2177 spp。以下是各 GPU Pass 均值之和，不包含 CPU/GPU 时间间隙，不是窗口 FPS；各版本单次短测，未作长期性能结论。

| 后端 | 之前 GPU Pass 总和 ms | 当前 ms | 之前/当前测量末 native 使用 MiB |
| --- | ---: | ---: | ---: |
| DX12 | 1.686 | .472 | 322.00 / 190.81 |
| Vulkan | 1.337 | .403 | 321.12 / 189.87 |

当前反射只执行 Trace/Accumulate/Statistics/Combine 四个 Pass；图中没有 HitGuide。另以 257×193、预热 2046/测量 1/额外截图核验恰好 2048 spp 的 Denoised 诊断。上述四对 BMP（双后端 × 2048 Denoised/2177 Final）均与旧版逐字节相同；586 张完整场景回归图像也全部相同。Release 回归检查 2047→2048 的 Pass 旁路，以及随后相机移动时 progressive/temporal 均冷启动。

限制：当前仍有高分辨率 FP32 时间历史的较大成本，活动 View 可超过缓存软目标但不能突破 native 总上限；4K 和其他 GPU 未验证。几何法线/稳健射线偏移、mip/纹理 footprint 需要独立的数据契约与尺度/细节验收，本轮没有以调整 epsilon 或经验 LOD 代替这些工作。没有改变采样分布、光照、亮度上限或参考精度。
