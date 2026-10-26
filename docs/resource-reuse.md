# 资源复用与拥有权

## 帧上传页

`rhi::UploadPool` 是 owner-thread 使用的有界整页缓存，默认每页 64KiB、最多 64 页。`Begin()` 返回不可复制的 Batch；池先 Collect 已完成提交，随后 `Write(bytes, alignment)` 返回持有 Buffer 的 Slice，包含 offset 和 size。默认偏移对齐为 256 字节；调用者仍负责提供符合绑定要求的数据大小。

同一 Batch 的切片顺序追加至页面，剩余容量不足才获取下一页。已有页面只有在不属于活动 Batch、记录的提交已完成、且 Buffer 仅由池内 Arena 持有时才 Reset。CPU Slice、Binding、Graph 和已录制未提交的 CommandList 都可以保活整个页面；GPU 退休不会使仍被旧 CPU 快照引用的页面可写。

完成本批全部写入后再录制，成功提交后调用 `Seal(completion)`，只能传入 Begin 后实际提交的完成点。析构释放活动占用；未 Seal 等同取消，不创造提交。即使取消后还保留命令或 Slice，页面仍受引用保护。再次提交旧命令也由 Runtime 的待完成引用保护。池及 Runtime 必须分别活得比 Batch 和 GPU 对象更久。

达到页数上限或单次写入超过页容量时明确报错，不隐式等待或无限扩容。`Stats()` 返回累计创建页数、当前保留字节、单 Batch 写入字节高水位（含对齐填充）和 Batch 次数。首版保留页至池销毁，不主动缩容；请求字节数和原生显存分配统计分别观察。

SceneRenderer 的 View/Lighting/Shadow/Object 与 Display 常量使用此池，按真实 offset/size 传递到 DrawPacket 和 Graph。每页在同一图只导入一次，均为录制前 HostWrite、图内 UniformRead；Graph 继续保守按整 Buffer 跟踪状态。材质的长期 Prepared 数据另行管理，不放入帧上传页。

双后端 RHI 验收覆盖非零偏移、取消后保留 Slice/录制命令、跨八批次后提交旧命令、GPU 完成后继续持有 CPU 数据、预算耗尽和非法 Seal；入口为 `gpu.dx12.rhi`、`gpu.vulkan.rhi`。
