# 本机固定开发版入口

用户约定的 Mengshee 开发版入口为：

`C:\Users\jairy\Documents\okular\windows_build\dist\mengshee-pdf\app\bin\mengshee.exe`

CMake 构建目录 `windows_build\build\mengshee-standalone` 和上述运行目录不同。仅构建成功，或在 build / 临时测试目录中验证，**不代表用户的开发版已经更新**。排查“开发版找不到入口”时，不要先假定用户打开错了版本。

发布安装包后，用户也可能运行 `C:\Program Files\Mengshee\bin\mengshee.exe`。应核对当前进程路径；不得把开发目录同步当成已更新安装版，也不要关闭有未保存文档的进程。

## 更新与验收

- 确认目标程序已关闭，不强制终止用户进程。
- 先备份，再同步相互匹配的应用程序、Core、Part 和 PDF 后端。功能有额外运行依赖时一并核对，不要盲目复制整个 SDK。
- 当前核心路径为 `bin\Okular6Core.dll`、`bin\plugins\kf6\parts\okularpart.dll` 和 `bin\plugins\okular_generators\okularGenerator_poppler.dll`。
- 核对部署文件与本次构建的哈希，并使用 **dist 自身的运行库/插件** 验证，不能用 SDK 的 PATH 掩盖部署缺失。
- 此包的 Qt 平台插件在 `bin\platforms`，其他插件位于 `bin\plugins`。在包外运行测试程序时，插件搜索路径须同时包含 dist 的 `bin` 和 `bin\plugins`。
- 修改工具栏入口时，要点击真实工具栏按钮验证，不能仅检查 QAction 存在或从菜单触发。
- 回归范围跟随改动：字体 hotfix 只做字体渲染/缺字回退、实际 PDF 对比和包内冒烟，不跑无关的模式切换矩阵（用户明确要求）。下述完整模式矩阵用于工具栏/模式布局改动，不作为每个 hotfix 的固定步骤。
- 修改 Poppler 渲染器（例如 `SplashOutputDev.cc`）后，要重建 SDK 并同步 `poppler.dll`、`poppler-qt6.dll`，不能只更新应用的 PDF 插件。
- 从 2026.0.22.4 起还必须同步 `app\share\fonts` 的 14 个私有 Base14 字体及许可/清单。这些是核心运行资源，不属于可选 StemTeX；SDK、开发目录、stage 都通过 `windows-build/cmake/pdf-base14-fonts.cmake` 校验。字体机制与专项验证见 [PDF 字体说明](pdf-font-rendering.md)。
- `external/poppler/utils/PdfPageSequenceEditor.cc` 作为应用内辅助库链接到 PDF 后端；修改它后须重建 `okularGenerator_poppler`，仅重建 SDK 的 `poppler.dll` 不会更新该代码。

阅读区域编辑模式现名 **Reading Views**；其中的 Views 是页面内阅读区域，不等同于传统 **View / 视图** 显示菜单。对应模式工具组在第二行**右对齐**，使用**纯图标**按钮，不显示文字；绘制、排序、生成、应用的名称和说明放在悬停提示中，Apply 使用一个区域复制到多个区域的图标；批注工具留在左侧。验收需覆盖宽、窄窗口、所有模式切换、布局恢复及 XMLGUI 重建，检查实际最后一个按钮的位置，不能只检查工具栏占满整行。伸缩留白必须注册在 XMLGUI 中，重建结束后重新落实第二行及右侧顺序；不能依靠临时插入的控件。
