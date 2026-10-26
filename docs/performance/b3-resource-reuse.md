# B3：资源复用优化结果

设备及配置沿用 [B2](b2-pbr-rtx5060.md)：RTX 5060、Release、validation off、PBR 1280×720/Medium/Final，串行 headless。各子阶段保留独立原始结果和可执行文件，不改变测量口径。

## B3.1 帧上传页

每后端三次、每次 1 秒预热/3 秒测量，交替后端顺序。DX12/Vulkan 每次运行均值的等权平均为 9.1022/2.9743ms。这是中间阶段短测量，不直接与 B2 的长测量相减承诺收益；最终优化前后比较另行执行。

另以每后端一次 1 秒预热/1 秒测量、CPU profiler on 检查调用计数：Buffer 创建由 B2 的 23 降至每帧 10 次，剩余为材质与 timestamp；逻辑 BindingGroup 仍为每帧 10 次。独立探针不混入前述均值。两组、两后端首轮截图与 B2 对应 720p BMP 全文件逐字节相同。

Debug/Release 完整 CTest 各 41/41、双后端 Validation 通过；新增上传测试包含 CPU 快照、取消、延后提交、GPU 退休后继续保留旧数据和页预算。资源页按引用与完成点共同复用，正常 Render 不增加 GPU Wait。

原始结果：out/benchmarks/b3-1-upload 与 out/benchmarks/b3-1-upload-probe。阶段二进制：out/benchmarks/b3-1-player.exe。

## B3.2 不可变材质常量

相同的三次短测量配置，DX12/Vulkan 等权帧均值为 5.4230/1.2512ms。独立 CPU 探针确认每帧仅剩一次 Buffer 创建（timestamp），逻辑 BindingGroup 仍为 10 次。两组两后端 BMP 与 B2 均逐字节相同。

Debug 全套其余 39 项通过，材质测试调整为仅检查材质块共享后双后端重跑通过；Release 全套 41/41。新增 70 个常量版本淘汰后重用旧 Prepared 和已录制命令，既有纹理编辑、热重载、窗口和双帧验证通过。原始组 out/benchmarks/b3-2-material{,-probe}，阶段二进制 out/benchmarks/b3-2-player.exe。

## B3.3 描述符

相同三次短测量配置，DX12/Vulkan 等权帧均值 1.3112/1.0650ms。独立探针原生描述符创建为 0 次/帧；DX12 实际 heap 绑定为 2 次/帧，逻辑 GroupCreate 仍为 10 次/帧。此时 Buffer 仍为一次 timestamp，ViewCreate 仍为 12 次/帧。BMP 与 B2 相同。

含预热的资源统计：上传页均为一页 64KiB，单批高水位 3328B；DX12 描述符为 2 页、4352 个保留 slot，Vulkan 为 10 块、120 个保留 slot；两者在用高水位均为 120，测量结束在用数为 0。描述符 slot 不等同于原生分配字节。两后端 Debug/Release 完整 CTest 各 41/41，Python 13/13；容量耗尽恢复及旧绑定组在 70 次绘制中保持正确。

原始组 out/benchmarks/b3-3-descriptors{,-probe}，阶段二进制 out/benchmarks/b3-3-player.exe。最终长基线与 B3.4 尚未执行。
