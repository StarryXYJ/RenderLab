# RenderLab

RenderLab 是面向引擎与渲染开发的 C++20 渲染引擎项目，目标包括 DX12/Vulkan 双后端、统一 Shader 编译、Shader 驱动材质、声明式 Render Graph、光栅与光追，以及可选 GPU Driven。后期编辑器采用 C#，渲染运行时保持独立。

当前已完成架构设计和仓库规范初始化，尚未创建 CMake 构建目标或渲染程序。实际实施进度以阶段计划为准。

## 项目入口

- [总体设计](design/architecture.md)：模块职责、数据契约和渲染架构。
- [阶段计划](design/phaseplan.md)：当前阶段、工作项、依赖和验收标准。
- [设计复查](design/design-review.md)：参考项目、设计风险与取舍。
- [工程约定](manual/conventions.md)：编码、换行、格式、命名和文件管理。
- [协作约定](AGENTS.md)：后续修改与阶段维护要求。

## 仓库配置

| 文件 | 用途 |
| --- | --- |
| `.editorconfig` | UTF-8 无 BOM、缩进、换行和保存规则 |
| `.gitattributes` | Git 文本规范化和二进制属性 |
| `.clang-format` | C++ 源文件格式 |
| `.gitignore` | 构建目录、本机设置、缓存和诊断产物 |

文本默认使用 LF，Windows 批处理脚本使用 CRLF。源码、项目资产、`.meta`、共享构建配置和依赖锁文件纳入版本管理；生成产物进入约定的输出目录。

## 目录职责

| 目录 | 内容 |
| --- | --- |
| `design` | 架构、设计决策和阶段计划 |
| `docs` | 已实现模块的接口与技术说明 |
| `manual` | 工程规范、构建与使用步骤 |
| `src` | 引擎、工具和应用源码 |
| `shaders` | 引擎内置 Shader |
| `sample` | 演示项目和源资产 |
| `tests` | 行为测试与小型参考数据 |
| `cmake` | 构建模块 |
| `external` | 已固定版本的第三方集成 |

目录随对应功能加入版本管理，不以占位代码或空构建目标表示功能已实现。下一步是 M0.1 的最小构建、工具链与依赖配置。
