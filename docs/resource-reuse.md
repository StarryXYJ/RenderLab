# 资源复用与拥有权

## 帧上传页

`rhi::UploadPool` 是 owner-thread 使用的有界整页缓存，默认每页 64KiB、最多 64 页。`Begin()` 返回不可复制的 Batch；池先 Collect 已完成提交，随后 `Write(bytes, alignment)` 返回持有 Buffer 的 Slice，包含 offset 和 size。默认偏移对齐为 256 字节；调用者仍负责提供符合绑定要求的数据大小。

同一 Batch 的切片顺序追加至页面，剩余容量不足才获取下一页。已有页面只有在不属于活动 Batch、记录的提交已完成、且 Buffer 仅由池内 Arena 持有时才 Reset。CPU Slice、Binding、Graph 和已录制未提交的 CommandList 都可以保活整个页面；GPU 退休不会使仍被旧 CPU 快照引用的页面可写。

完成本批全部写入后再录制，成功提交后调用 `Seal(completion)`，只能传入 Begin 后实际提交的完成点。析构释放活动占用；未 Seal 等同取消，不创造提交。即使取消后还保留命令或 Slice，页面仍受引用保护。再次提交旧命令也由 Runtime 的待完成引用保护。池及 Runtime 必须分别活得比 Batch 和 GPU 对象更久。

达到页数上限或单次写入超过页容量时明确报错，不隐式等待或无限扩容。`Stats()` 返回累计创建页数、当前保留字节、单 Batch 写入字节高水位（含对齐填充）和 Batch 次数。首版保留页至池销毁，不主动缩容；请求字节数和原生显存分配统计分别观察。

SceneRenderer 的 View/Lighting/Shadow/Object 与 Display 常量使用此池，按真实 offset/size 传递到 DrawPacket 和 Graph。每页在同一图只导入一次，均为录制前 HostWrite、图内 UniformRead；Graph 继续保守按整 Buffer 跟踪状态。材质的长期 Prepared 数据另行管理，不放入帧上传页。

双后端 RHI 验收覆盖非零偏移、取消后保留 Slice/录制命令、跨八批次后提交旧命令、GPU 完成后继续持有 CPU 数据、预算耗尽和非法 Seal；入口为 `gpu.dx12.rhi`、`gpu.vulkan.rhi`。

## 不可变材质常量

Material Runtime 在每个 GPU 包代际中保留至多 64 个常量版本，以 block 布局 hash 和完整打包字节查找。目标后端由 Runtime 固定；跨包代际不复用。Pack 的 padding 为确定的零值，内容比较不只依赖 hash。命中只共享只读 Buffer，纹理/采样器仍逐次解析，View/Object 和其他外部块不进入此缓存。

数值改变产生新版本；缓存 FIFO 淘汰仅释放缓存拥有权，Prepared 和命令持有的旧 Buffer 继续有效。CPU 强持有缓存使串行帧之间也能命中；上限对应最多 1MiB 打包数据（每块最多 16KiB），原生分配的实际字节由 RHI ledger 另计。整代热重载仍使用原有发布协议。

常量 Buffer 创建后不再写入。`Prepared::constants` 包含缓存命中版本，供图外调用者继续发出保守 HostWrite→UniformRead 可见性依赖；这不表示每次 Prepare 都发生 CPU 写入。Upload Buffer 在 DX12 保持原生 GENERIC_READ，在 Vulkan 没有图像布局转换，重复的 HOST_WRITE 来源依赖覆盖最初上传；首版不为省去该依赖另建可变的全局状态表。

## 描述符

DX12 Runtime 持有资源 heap 页（每页 4096 项）和 sampler heap 页（每页 256 项），每类最多八页，均在基础 API 限制内。BindingGroup 以 first-fit 分配每类的连续区间，持有区间和原生 heap；区间释放不分配内存，也不搬迁存活内容。每组同类描述符只来自一页，设置新的 heap 组合后重新设置所有 root table。同命令列表内连续使用同一组合时省去重复 SetDescriptorHeaps。

Vulkan 首版复用与 BindingLayout 身份关联的 pool/set 块，每块包含四个 set。最多保留 256 块，优先复用无外部引用的同布局块；达到预算时只淘汰空闲块，全部被引用则明确报错。不按 CPU 帧号 Reset pool，不改用 dynamic uniform offset 或 update-after-bind。

两后端均由逻辑 BindingGroup 的拥有权保护分配：CPU 保存的组、已录制未提交命令和 Runtime 的待完成命令包都会阻止重用。取消只释放 CPU 引用；已提交组在真实完成后退休。缓存的分配页/块不拥有 Buffer/Texture，避免长期钉住 Graph 资源和上传页。构造失败通过 RAII 归还已取得的区间。

`Runtime::GetDescriptorStats()` 返回当前原生页/块数、容量项数、在用项数、高水位和累计创建次数。这里的单位是 descriptor slot，不是字节；不并入 RHI Buffer/Texture 的分配 ledger。统计共享状态与租约同寿命，不捕获可能已失效的分配器地址。首版缓存至 Runtime 销毁；Vulkan 另有满预算时的空闲淘汰。

Benchmark 报告增加可选 `resourceReuse.descriptors` 和 `resourceReuse.frameUpload`，在测量结束、截图前读取累计及当前统计（含预热）。CPU 探针另有 `rhiDescriptorPageCreateMs`（DX12 shader-visible heap / Vulkan pool 创建）和 `rhiDescriptorHeapBindMs`（DX12 实际 heap 绑定）；为包含关系的墙钟区间，不能与 GroupCreate/NativeSubmit 相加。

## Timestamp 与默认 View

Graph profiling 从同一个有界 Graph Pool 取得 512 字节 Readback Buffer，覆盖 64 个 timestamp slot。录制前按该分配的实际状态执行 CopyDest 依赖，成功提交才发布 HostRead 和 completion；失败不发布。GpuProfile 的 ticks 引用保护 CPU 读取期，连同图租约、命令引用和完成点共同限制复用。可以多次 Read 已完成的结果；仅等待 Fence 后保留的结果仍不可被后续图覆盖。

默认 TextureView 为不可变对象，`CreateView(texture)` 可以返回同一存活实例。Texture 内只保存 weak_ptr，View 强持有 Texture，因此没有循环引用。Graph Pool 的物理 Allocation 强持有默认 View；Available 同时检查分配租约、完成点、View 外部引用以及 Texture 的已知内部引用数（Allocation 加默认 View）。保留 View 的 CPU/命令引用也会阻止 Trim。

可选的瞬态纹理 Alias 另按完整 Group 租用与退休，任意成员的 View 都保护整组；共享 backing 只计费一次，普通 Acquire 不取得 Alias 成员，见 [内存 Alias](memory-alias.md)。

静态资源使用其已有的 View；SceneRenderer 只额外持有一个 offscreen Display View。窗口交换链持有每张图像的 View，并在等待展示完成后的 resize 路径先清 View、再释放图像和重建。Renderer 不缓存交换链目标，避免 ResizeBuffers 被隐式引用阻挡；Vulkan 在销毁旧 swapchain 前同样清理 View。

`rhiViewCreateMs` 从 B3.4 起只覆盖默认 View 缓存未命中时的实际对象构造，缓存查询不计为创建。B2/B3.3 之前未缓存，每次调用都构造对象。双后端图验收保留一个已完成的 GpuProfile，再运行八个图并重复读旧结果；需保持 tick 字节不变、只用两份读回分配，最后释放结果后 Trim 清空。CPU 测试另验证保留 View 阻止复用、内部 View 不阻止合法复用、无引用环。
