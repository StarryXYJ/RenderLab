# B3：资源复用优化结果

设备及配置沿用 [B2](b2-pbr-rtx5060.md)：RTX 5060、Release、validation off、PBR 1280×720/Medium/Final，串行 headless。各子阶段保留独立原始结果和可执行文件，不改变测量口径。

## B3.1 帧上传页

每后端三次、每次 1 秒预热/3 秒测量，交替后端顺序。DX12/Vulkan 每次运行均值的等权平均为 9.1022/2.9743ms。这是中间阶段短测量，不直接与 B2 的长测量相减承诺收益；最终优化前后比较另行执行。

另以每后端一次 1 秒预热/1 秒测量、CPU profiler on 检查调用计数：Buffer 创建由 B2 的 23 降至每帧 10 次，剩余为材质与 timestamp；逻辑 BindingGroup 仍为每帧 10 次。独立探针不混入前述均值。两组、两后端首轮截图与 B2 对应 720p BMP 全文件逐字节相同。

Debug/Release 完整 CTest 各 41/41、双后端 Validation 通过；新增上传测试包含 CPU 快照、取消、延后提交、GPU 退休后继续保留旧数据和页预算。资源页按引用与完成点共同复用，正常 Render 不增加 GPU Wait。

原始结果：out/benchmarks/b3-1-upload 与 out/benchmarks/b3-1-upload-probe。阶段二进制：out/benchmarks/b3-1-player.exe。最终长基线及其他子阶段尚未执行。
