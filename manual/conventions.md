# RenderLab 工程约定

本约定适用于手工维护的源码、Shader、配置、文档和项目源资产。生成文件与第三方代码保留其原有格式，通过构建和导入流程管理。

## 编码与换行

| 项目 | 约定 |
| --- | --- |
| 文本编码 | UTF-8，无 BOM |
| 默认换行 | LF |
| Windows 批处理 | `.bat`、`.cmd` 使用 CRLF，命令内容优先保持 ASCII |
| 缩进 | 空格，默认 4 个 |
| JSON 与 YAML | 缩进 2 个空格 |
| Markdown | 列表缩进 2 个空格，保留有意使用的行尾双空格 |
| 文件末尾 | 保留一个最终换行 |
| 普通行尾 | 删除空白，Markdown 的显式换行除外 |

`.editorconfig` 定义保存规则，编辑器需要启用 EditorConfig 支持。使用 PowerShell 写文件时明确选择 UTF-8 无 BOM，避免 Windows PowerShell 默认编码或工具默认代码页产生不同结果。仓库开发脚本使用 PowerShell 7 或其他明确支持 UTF-8 的工具。

`.gitattributes` 将识别出的文本按 LF 规范化，二进制文件禁止文本转换。它不验证 UTF-8 编码；编码仍需由编辑器保存规则和提交前检查保证。工作区级别的属性覆盖本机 `core.autocrlf` 的换行选择，不修改开发者的全局 Git 配置。

不要对整个资产目录强制文本转换。`.obj` 同时可能是 Wavefront 模型和编译目标文件，保留 Git 内容检测；模型不能因扩展名被全局忽略。图片、压缩包、原生库和 Shader 二进制按二进制处理。

## C++ 与 Shader

C++ 采用 C++20，格式以根目录 `.clang-format` 为准：4 空格、100 列、Allman 大括号、左侧指针与引用符号。格式化只针对自有 C++ 源文件，不递归改写 external、生成目录或导入资产。

新增 C++ 文件使用 `snake_case.hpp` 和 `snake_case.cpp`。公共头文件明确包含其使用的标准库与其他头文件，不依赖传递 Include。命名采用以下约定：

| 对象 | 约定 | 示例 |
| --- | --- | --- |
| 命名空间 | 小写，按模块组织 | `renderlab::rhi` |
| 类型、枚举类型 | PascalCase | `TextureDesc`、`QueueType` |
| 函数与方法 | PascalCase | `CreateTexture`、`AddPass` |
| 局部变量与参数 | camelCase | `frameIndex`、`textureDesc` |
| 私有数据成员 | camelCase 加尾部下划线 | `frameIndex_` |
| 枚举值与具名常量 | PascalCase | `QueueType::Graphics` |
| 必要的宏 | `RENDERLAB_` 前缀 | `RENDERLAB_ASSERT` |

自有标识符使用 ASCII，注释、诊断和文档可以使用中文。运行时普通窄字符串约定为 UTF-8；调用 Win32 宽字符 API 时在平台边界显式转换，路径使用明确的 filesystem/平台转换流程，不依赖系统 ANSI 代码页。

M0.1 的构建配置必须显式启用 C++20、关闭编译器语言扩展，并对 MSVC 自有目标启用 `/utf-8`。警告配置作用于自有 Target，不把项目规则传递给第三方。完成构建验证后，在使用说明中固定实际工具链版本。

HLSL 使用 `.hlsl` 与 `.hlsli`，遵守 UTF-8、LF、空格和命名约定。C++ formatter 不自动视作 HLSL formatter；正式 Shader 工具链阶段再确定合适的格式化工具。Shader 属性的稳定 ID 与显示名、代码 symbol 分开维护。

安装 clang-format 后，可对指定的自有 C++ 文件执行：

```powershell
clang-format --dry-run --Werror path/to/file.cpp
clang-format -i path/to/file.cpp
```

团队和后续 CI 使用一致的 clang-format 版本，避免版本差异造成无意义改动。当前尚未固定和验证工具版本，不把配置文件存在当作格式化已执行。

## C# 编辑器

C# 文件使用 UTF-8 无 BOM、LF、4 空格和 Allman 大括号，新文件使用 file-scoped namespace。类型与公共成员使用 PascalCase，局部变量与参数使用 camelCase。

EditorConfig 提供基础规则。正式进入编辑器阶段时再固定 .NET SDK、Avalonia 和分析器版本，不预先添加空解决方案、SDK 锁定或尚未使用的包。

## 构建与资源文件

共享 CMake 配置与 `CMakePresets.json` 纳入版本管理，本机路径和个人选项写入被忽略的 `CMakeUserPresets.json`。构建树位于根目录 build、build-* 或 out 等已忽略目录，使用 out-of-source 构建，不向源码目录输出编译文件。

依赖版本或提交必须固定，锁文件纳入版本管理。下载、编译和包恢复产物放在输出目录；external 中只提交有意维护的集成文件或明确需要的依赖内容。

项目 Assets、资源 `.meta`、ProjectSettings、Shader 元数据和项目清单是源数据。Library 是可重建缓存，Builds 是导出结果，UserSettings 是本机设置。资源资产使用相对引用或稳定 ID，不保存开发机器绝对路径。

临时 Shader/Pipeline 缓存使用 cache 或项目 Library；日志使用 logs；本机 GPU Capture 使用 captures。需要长期保留的基准结果与 Capture 说明放在 docs 下对应目录，以免被本机诊断目录的规则忽略。

## 修改与检查

提交前检查修改范围，不顺带重排第三方代码或批量转换二进制文件。文档和共享配置应能直接读取中文，文本不包含 BOM、意外 CRLF 或替换字符。

可用以下命令检查典型路径的 Git 属性与忽略规则，路径只用于查询，无需先创建文件：

```powershell
git check-attr text eol diff -- src/core/example.cpp sample/Example/Assets/image.png
git check-ignore -v --no-index -- build/CMakeCache.txt sample/Example/Library/cache.bin
git status --short
```

Git 首次提交前没有历史可供比较，仍需检查所有待加入文件。不要通过改动全局 Git 配置或静默安装钩子实现仓库约定。后续接入自动检查时复用这些配置，避免增加另一套格式规则。
