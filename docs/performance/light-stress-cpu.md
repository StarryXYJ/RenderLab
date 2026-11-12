# 多物体光照场景 CPU 排查

2026 年 11 月 12 日，Windows Release，RTX 5060，DX12，960×540，`LightStress.rlproject`，固定相机，validation off、CPU profiling on、5 帧预热和 30 帧测量。Forward 直接提交在阴影复用前/后，串行帧均值 29.42/25.32 ms，场景准备均值 20.25/16.22 ms，Graph 命令录制均值 7.42/7.48 ms；两次 BMP 的 SHA-256 都为 `ffc431963978340f652c2d65f22997928144bd0fbca90cd392df53e08b588841`。这是单次短测，不能推断窗口 FPS 或其他硬件性能，CPU profiling 会引入额外成本。

同样条件下未优化版使用 `--submission indexed-direct` 的串行帧为 21.01 ms，场景准备为 14.59 ms；这只是实验选项，不代表所有场景可用。DX12 的 Deferred 优化后图像与旧版本相同，混合管线因累积帧数不同未做像素等价结论。两后端的 `gpu.*.raster_area_light` 和 `gpu.*.area_light_reflection` 共 4 项通过。测试命令参照 [构建手册](../../manual/build.md#多物体光照压测)，添加 `--cpu-profile --validation off --benchmark 30 --warmup-frames 5`；验证时启用 GPU 测试并运行对应 CTest。

当前仍有大量单对象准备和绘制记录开销，不能认定卡顿已经解决。Vulkan 大场景描述符块预算耗尽、窗口交互、长时性能、其他硬件和并行点光源改动组合均未验证。
