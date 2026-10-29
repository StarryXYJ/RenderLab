# 瞬态纹理内存 Alias

`CompileOptions.memoryAlias` / `SceneRenderOptions.memoryAlias` 独立于 CPU 录制线程和异步 Compute，默认 false。场景命令行使用 `--memory-alias`，Benchmark case 使用布尔值 `memoryAlias`。关闭时仍使用同一执行器与普通资源池。

## 规划边界

编译器只考虑未导入、未导出且存活的瞬态 Texture；History 的持久导入/导出也因此排除。每个候选的所有访问必须位于 Graphics Queue，并且首次访问是 Raster 附件 Clear。深度/Stencil 两个存在的 aspect 都必须 Clear 且可写。Storage 首写、Copy 首写、Discard 和 Load 不作为首次初始化证明。

同组匹配格式与 attachment/storage 用途，允许尺寸不同，最多 16 个成员；前一成员最后访问严格早于下一成员首次访问。此位置比较的依据是同一原生 Graphics Queue 的实际有序提交，不能把不同队列的拓扑序当作 happens-before。异步首版只有 Buffer feeder，不访问 Alias Texture；后续若扩大队列支持，候选过滤仍必须保留。导入外部 `aliasBacking` 纹理会被拒绝，避免图外隐藏重叠。

`Plan.aliasGroups` 和资源的 `aliasGroup` 给出成员身份，准备后的 `aliasBackingBytes` 是本图共享 backing 的原生字节和；`first`/`last` 是调度位置，调度项的 `aliasActivations` 标出替代普通首次屏障的激活。普通资源版本/RAW/WAR/WAW、Cull 和最终状态规则保持有效。真实机会包括光栅 Shadow 的私有深度附件到 CameraDepth；仍被后续采样的 Shadow 颜色不会提早复用。

## 原生分配与激活

RHI 的 `QueryAliasedTextureBytes` 先查询共享分配大小，使 Pool 能先清理可退休缓存，再创建原生内存。`CreateAliasedTextures` 再次验证，返回共享 backing 的纹理。`Texture.aliasBacking` 保活 Heap/Memory；派生纹理先销毁原生 image/resource，最后才释放 backing。账本只计一次实际 backing 字节。

- DX12：`GetResourceAllocationInfo` 查询每个成员；同一 RT/DS Heap 的 offset 0 上创建 placed resources，大小覆盖全部成员。激活使用 aliasing barrier，再按该 resource 自身上次提交的实际状态转换到附件状态。下一次使用必须完整 Clear，初始化 placed RT/DS 元数据。
- Vulkan：使用 `VK_IMAGE_CREATE_ALIAS_BIT`、最优平铺，查询 requirements2/dedicated requirements，取最大 size/alignment 与 memoryTypeBits 交集。要求 dedicated 的纹理被明确拒绝；不共享不兼容内存类型。offset 0 同时满足全部对齐。激活建立跨别名的全局内存依赖，再以旧布局 UNDEFINED 转换到目标附件布局；重叠写已使另一 image 的内容/元数据失效，不能沿用它的历史布局保留内容。

RHI 激活命令只允许附件初始化状态，命令包保活前后纹理。Graph 用激活替代成员首次普通转换；之后仍跟踪每个逻辑纹理的状态。使用者不能在失活后访问旧成员。

## 池与提交事务

普通条目和 Alias Group 分开缓存，普通 Acquire 不会取得 Alias 成员。每组保存全部成员、各自已提交状态/完成点、最后活动成员与共享字节数。逻辑资源数按所有成员计入容量，字节只计共享块一次；普通资源估算字节仍不是原生对齐分配数，实际显存比较使用 RHI MemoryStats。

组只有在所有成员 lease、Texture、View 和命令引用都释放，且真实整帧完成点退休后才能复用或 Trim。任意一个成员的外部 View 都保活整组。清理、容量腾挪按组整体操作，不留下可被单独复用的成员。

准备阶段租用整组；录制仅在本地推进活动成员，不修改缓存状态。Submit 成功才发布全部成员最终状态、真实完成点和最后活动成员。取消/首次入队前失败释放租约，保留上次已提交状态；部分原生入队失败由 Runtime 保活并 poison，排空之前不释放命令，禁止继续提交未知状态的工作。此规则与 [异步 Compute](async-compute.md)、[并行录制](parallel-recording.md) 的完成点契约一致。

场景切换 Alias 模式时只清理可退休池缓存，在途与外部持有资源继续保护。独立 Benchmark 进程避免混合两种模式的历史缓存。默认关闭便于基线与诊断；内存收益和时间代价分别测量。
