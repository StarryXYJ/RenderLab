# RenderLab 工作约定

- 开始实现或修改架构前，阅读 `design/architecture.md` 和 `design/phaseplan.md` 中的当前阶段。设计理由见 `design/design-review.md`。
- 文件编码、换行、命名与格式遵守 `manual/conventions.md`、`.editorconfig`、`.gitattributes` 和 `.clang-format`。手工维护文本使用 UTF-8 无 BOM，默认 LF；二进制与第三方文件不批量转换。
- `design/phaseplan.md` 是阶段任务与状态的唯一记录入口。按用户本次授权逐步推进，不因计划覆盖整个项目而自动执行全部阶段。
- 开始阶段时更新当前工作项；完成子阶段时记录实际验证结果并精简已完成的详细待办，保留结果、验证入口与限制。
- 主阶段满足退出条件后才标记 DONE，并把该阶段压缩成完成摘要。未运行或硬件未验证的事项明确保留，不能当作通过。
- 长期接口说明写入 `docs`，构建与使用步骤写入 `manual`，计划链接这些说明而不重复内容。架构变化同步更新总体设计。
- 已完成能力被后续修改影响时，重新标记阶段或登记修复项，保持状态真实。文档修改本身不计作实现阶段完成。
