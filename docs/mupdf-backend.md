# MuPDF 后端迁移

本分支默认使用 `MENGSHEE_PDF_BACKEND=MuPDF`。MuPDF 固定为子模块
`external/mupdf` 的 `1.28.5`（`8ad45e92f0935d3d87f1db3f873086472a5e1b24`）。
Windows 插件为 `okularGenerator_mupdf.dll`，MuPDF 静态链接；部署脚本不再
携带 Poppler DLL、Poppler 插件和 Poppler CMap 数据。

这是一条可构建的迁移分支，**还不是与本地 Poppler fork 功能完全等价的发布版本**。
保留 `external/poppler` 和显式 `-DMENGSHEE_PDF_BACKEND=Poppler` 用于对照和回退。
原 Linux 构建脚本显式选择 Poppler；本次验证目标为 Windows x64。

## 本机配置与构建

- Qt：`C:/Qt/6.11.1/msvc2022_64`
- Visual Studio 18 Community，MSVC v145，C++20
- CMake：`C:/Qt/Tools/CMake_64/bin/cmake.exe`
- Ninja：`C:/Qt/Tools/Ninja/ninja.exe`
- workspace：仓库内 `windows-build-workspace`
- SDK：`windows-build-workspace/sdk`；MuPDF 独立放在 `sdk/mupdf`

在仓库根目录 PowerShell 执行：

```powershell
git submodule update --init --recursive external/mupdf
$cmake = 'C:/Qt/Tools/CMake_64/bin/cmake.exe'
$workspace = "$PWD/windows-build-workspace"
& $cmake "-DWORKSPACE_ROOT=$workspace" -P windows-build/cmake/build-mupdf-sdk.cmake
& $cmake "-DWORKSPACE_ROOT=$workspace" `
  "-DBUILD_DIR=$workspace/build/mengshee-mupdf" `
  "-DINSTALL_PREFIX=$workspace/install/mengshee-mupdf" `
  -DMENGSHEE_PDF_BACKEND=MuPDF -DSKIP_PACKAGE=ON `
  -P windows-build/cmake/build-windows.cmake
```

这复用现有 Qt/KF6/QScintilla/StemTeX SDK。SDK 脚本可用 `MSBUILD`、
`PLATFORM_TOOLSET`、`JOBS` 覆盖默认值。MuPDF 上游 MSVC archive 已包含
Tesseract，插件使用随 SDK 安装的匹配头文件，不能再链接外部 Tesseract DLL。
英语识别仍需要原有 `eng.traineddata`。OCR 回归测试也需将其放在测试程序旁的 `data/mengshee/tessdata/` 中。

运行：`windows-build-workspace/install/mengshee-mupdf/bin/mengshee.exe`。
该目录与原有 `dist/mengshee-pdf/app` 隔离。

## 已适配的路径

- 文件/内存打开、密码、页面渲染、文本提取、文档属性、字体清单及嵌入文件读取。
- 原生 PDF 保存到临时文件，再用 `QSaveFile` 原子替换目标；重新接入保存后的文件时
  刷新页面对象，打开失败则恢复原文档。
- 文本、几何、线条、高亮、墨迹、印章等常用注释；LaTeX 矢量外观、
  `LatexNoteData`、Reading Views 和编号标注元数据；跨文档复制印章外观。
- 注释按类型访问几何及边框属性；单个注释读取/绘制错误隔离并提示，保留原始 PDF 对象，继续处理正文和其他注释。
- 空白页插入、删除/恢复、移动、旋转、页面尺寸、页面复制/导入/合并。
  导入会复制注释对象及引用关系，并处理命名目标和表单字段重名。
- 文本、复选/单选和选择列表表单的读写；表单显示文本与实际值分别处理。
- 目录读取/编辑、命名目标和链接编辑、可见 note-link 标签、保留交互链接和表单的
  注释扁平化导出。扁平化使用现有 PDF 外观，保留 LaTeX 矢量内容；与链接或表单重叠不会阻止导出。
- 英语 OCR、布局分析、Unicode 隐形文本层写入和替换。
- Windows 构建、部署、许可证复制和 PDF 插件加载冒烟检查。

## 尚待验证和功能边界

- 真实文件中的复杂表单 JavaScript/动作、签名检查、任意注释类型和自定义外观
  仍未达到原后端完整覆盖；本机旧 Poppler SDK 也以 `ENABLE_NSS3=OFF` 构建，
  因而签署不能作为此次已通过功能。
- 合并目前保留页面内容、注释、链接、命名目标和表单；源文件的目录、附件及其他
  文档级元数据不自动合并。需要保留这些信息时不要直接使用页面合并结果替代源文件。
- 旋转/裁剪页的复杂 LaTeX 布局、真实 OCR 识别质量及跨文档编辑撤销，需要人工回归。
- 打印使用最大 300 DPI 栅格页面，尚未迁移原后端的全部打印选项。
- MuPDF 外观边界检测使用 bbox device，与旧 Poppler 检查器的字体轮廓精度、
  超时/资源限制策略不完全相同；不应据此宣称全部旧边界测试已经通过。

人工检查建议按顺序进行：打开实际 PDF → 搜索/选择文字 → 新建并编辑 LaTeX 标注
→ 保存并重新打开 → 复制页面、表单和跨文档标注 → 点击目录及 note-link
→ 扁平化另存 → OCR → 打印预览。每一步先使用文档副本。

## 验证

Windows 打印预览使用独立快照在后台按需渲染当前页，最高 72 DPI；120 ms 合并连续选项变化，过期结果丢弃，页面图像缓存限制为 64 MiB。份数不触发渲染，灰度、纸张和方向优先复用页面图像；正式打印分辨率不变。`printpreview_test` 4 项通过（含初始化和清理），覆盖界面响应、过期请求、缓存和关闭时不等待后台任务；后端测试验证未保存注释及源文档关闭后的快照渲染。

矩形编辑共用 `part/rectangleinteraction.h` 的命中、光标、拖动和绘制逻辑；调用方保留各自配色。`rectangleinteraction_test` 的 5 项测试通过（含初始化和清理），覆盖控制点重叠、拖动起点、对边固定、最小尺寸和选框内部透明。

2026-10-19 本机结果：完整 Windows 构建及运行目录部署成功；QtTest
61 passed、0 failed（含初始化和清理）；`file1.pdf` 及用户提供的 Zhai2026 PDF 启动冒烟成功，窗口响应正常，
实际加载 `okularGenerator_mupdf.dll`，未加载 Poppler，运行目录外项目 DLL 数量为 0。
2026.0.26.0 安装包已生成，打包目录启动检查通过；实际安装/升级及上述功能边界仍需验证。

`mupdf_backend_test` 是显式构建的测试目标（默认构建不包含它）：
以下编译命令在 VS x64 Developer PowerShell 中执行。

```powershell
& $cmake --build "$workspace/build/mengshee-mupdf" --target mupdf_backend_test --parallel 8
$env:PATH = "$workspace/sdk/bin;C:/Qt/6.11.1/msvc2022_64/bin;$env:PATH"
$env:QT_QPA_PLATFORM = 'offscreen'
$test = Start-Process "$workspace/build/mengshee-mupdf/bin/mupdf_backend_test.exe" `
  -ArgumentList '-o',"$workspace/tmp/mupdf-test-results.txt,txt" -Wait -PassThru -WindowStyle Hidden
$test.ExitCode
```

可设置环境变量 `MENGSHEE_TEST_PDF` 指向本地 PDF，额外验证逐页渲染、另存副本和重开；不设置时该项跳过。原文件不会被改写。

覆盖四种页面旋转、四条边和两种 UserUnit 下的页面尺寸调整、内容位置保持、保存重开及反向恢复；OCR 快照原子替换、实际英文识别/重复运行/跳过已有文字及临时文件清理、OCR 独立字体资源及真实 PDF 前后逐像素一致性、单词编辑范围精确往返及保存重开、不同显示 DPI 下的页面逻辑像素/物理点数换算、LaTeX AP 回归、坏注释隔离及保存保留、六类几何驱动注释、渲染保存、普通注释、页面/阅读视图、LaTeX 外观及拖动期间 AP 保留（含另存重开与图像一致性）、Unicode OCR、表单及附件、
目录和 note-link、扁平化、关闭源文件后的外观粘贴、同名目标合并、文件替换后
恢复删除页、错误输入返回、复制页注释独立性和外观页边界。运行目录冒烟：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows-build/scripts/smoke-test-mengshee-stage.ps1 `
  -StageRoot "$workspace/install/mengshee-mupdf" -PdfPath "$PWD/autotests/data/file1.pdf"
```

MuPDF 的 `COPYING` 随运行目录放在 `share/licenses/mupdf/COPYING`。

## 共享运行库

MuPDF 静态 SDK 只链接进 `MengsheePdfRuntime.dll`。PDF 插件通过工厂入口创建其中的后端，界面通过导出的页面尺寸与外观边界接口调用同一运行库。`okularpart.dll` 不再直接链接 MuPDF；打包与启动检查要求共享运行库随应用部署。OCR 功能保持完整。

MuPDF 运行包不再携带独立的 `tesseract55.dll` 与 `leptonica-1.87.0.dll`；OCR 已包含在共享运行库内。Poppler 构建仍保留这些动态依赖。安装器使用 LZMA2 ultra64，2026.0.26.0 安装包最终为 54.79 MiB；精简后运行目录通过 63 项后端测试和 PDF 启动检查。

## Windows 中日韩字体

后端通过 DirectWrite 查询系统字体文件及 TTC 字体索引。未嵌入的 CJK 字体先按 PDF 字体名查找，找不到时按 Adobe GB/CNS/Japan/Korea 字符集和衬线属性选择系统字体；没有对应系统字体则退回 MuPDF 内嵌字体。PDF 自身嵌入的字体仍优先使用，西文字体替代逻辑保持不变。字体回调也用于独立打印预览及 LaTeX 外观边界计算。

`windows-build/cmake/build-mupdf-sdk.cmake` 对 libmupdf 启用 `TOFU_CJK_LANG`，去掉 Source Han Serif 集合，保留 DroidSansFallbackFull。构建选项通过 MSBuild 导入文件传入，不修改上游子模块。配置变化后须重建 SDK 并重新链接应用。

回归测试使用无 FontFile 的 STSong-Light / UniGB-UCS2-H PDF，与嵌入系统 SimSun 的参考 PDF 做逐像素比较，并检查保存重开；另验证 Droid 兜底包含简繁中文、日文和韩文的样例字形。系统字体替代会使原本依赖内嵌思源宋体的文档外观发生变化。
