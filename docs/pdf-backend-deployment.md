# PDF 后端 phase 2 部署说明

本文说明“单插件 + 运行时引擎模块”的 Windows 部署改动。phase-1 的代码已经存在，
但此前没有 install/deploy 规则；本阶段把下面三个产物纳入安装与打包，同时保留旧
`okularGenerator_mupdf` / `okularGenerator_poppler` 作为兼容回退路径。

- `okularGenerator_mengshee_pdf.dll`：唯一对 Okular 暴露的 PDF 插件；
- `MengsheePdfEngineMuPdf.dll`：MuPDF 引擎模块（MuPDF、Tesseract 静态链接在模块内，
  但模块本身 import `MengsheePdfRuntime.dll`）；
- `MengsheePdfEnginePoppler.dll`：Poppler 引擎模块（由 `okularGenerator_poppler.dll`
  复制而来，运行时依赖 `poppler.dll`、`poppler-qt6.dll`、`tesseract55.dll`、
  `leptonica-1.87.0.dll` 和 `share\poppler` 的 CMap/CID 数据）。

## 1. 部署形状与开关

部署有两个互斥形状，由 `MENGSHEE_PDF_ENGINE_MODULES` 选择：

| 值 | 形状 | 插件目录内容 | 适用阶段 |
| --- | --- | --- | --- |
| `OFF`（默认） | 单后端 legacy | 只装当前 `MENGSHEE_PDF_BACKEND` 的旧插件 | 现在的默认路径，保持兼容 |
| `ON` | phase-2 双引擎 | 单插件 + 两个引擎模块 + 两个旧插件（运行时只加载所选的那一个引擎模块） | 验证/切换阶段 |

默认保持 `OFF`，所以现有 `build-mengshee.cmd` 的行为不变。打开方式：

```powershell
.\windows-build\build-mengshee.cmd -DMENGSHEE_PDF_ENGINE_MODULES=ON
```

`ON` 会同时把该开关传给 CMake configure（启用根 `CMakeLists.txt` 里的 phase-2
install 规则）、`deploy-runtime.cmake` 和 `package-windows.cmake`。

### 1.1 两个引擎来自两棵配置树

`generators/` 仍按 `MENGSHEE_PDF_BACKEND` 只配置一个引擎（`okularpart` 也还链接
一个后端），因此一个构建树只能产出一个引擎模块。phase-2 部署在 **deploy 阶段**
合并两棵树：

- 主配置：`MENGSHEE_PDF_BACKEND`、`BUILD_DIR`、`INSTALL_PREFIX` 指定的那棵；
- 另一配置：默认从
  `windows-build-workspace\build\mengshee-mupdf`、
  `windows-build-workspace\build\mengshee-poppler`、
  `windows-build-workspace\build\mengshee-standalone` 及对应 `install\` 目录中查找，
  也可以用 `-DMUPDF_ENGINE_ROOT=<dir>` / `-DPOPPLER_ENGINE_ROOT=<dir>` 显式指定。
  目录可以是 install 树，也可以是 build 树（脚本同时认
  `bin\plugins\mengshee_pdf\` 和 `bin\`）。

建议两棵树来自同一份源码修订。模块 ABI 由
`EngineModuleApiVersion`（当前为 4）和 `buildInfo` 校验，但不同修订的引擎/前端仍可能行为不一致。

先构建另一配置（只构建和 install，不 deploy/package，避免覆盖主舞台）：

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DMENGSHEE_PDF_BACKEND=Poppler `
  -DMENGSHEE_PDF_ENGINE_MODULES=ON `
  -DBUILD_DIR=C:/Users/jairy/Documents/mengshee/windows-build-workspace/build/mengshee-poppler `
  -DINSTALL_PREFIX=C:/Users/jairy/Documents/mengshee/windows-build-workspace/install/mengshee-poppler `
  -DSKIP_DEPLOY=ON -DSKIP_PACKAGE=ON `
  -P windows-build/cmake/build-windows.cmake
```

然后正常构建/部署主配置（示例为 MuPDF 主配置）：

```powershell
.\windows-build\build-mengshee.cmd -DMENGSHEE_PDF_BACKEND=MuPDF -DMENGSHEE_PDF_ENGINE_MODULES=ON
```

`build-windows.cmake` 在 deploy 之前会预检两棵树的引擎模块、旧插件、
`MengsheePdfRuntime.dll` 和单插件；缺失时会在改动 install 树之前直接失败并打印
另一配置的构建命令。

## 2. 安装布局

install 树、舞台和安装包中的相对布局一致：

```text
bin/
  mengshee.exe
  MengsheePdfRuntime.dll                 MuPDF 运行时（phase-2 为 MuPDF 引擎模块所需）
  poppler.dll                            Poppler 运行时
  poppler-qt6.dll
  tesseract55.dll                        Poppler 引擎的 OCR 依赖
  leptonica-1.87.0.dll
  plugins/
    okular_generators/
      okularGenerator_mengshee_pdf.dll   phase-2 单插件（priority 5）
      okularGenerator_mupdf.dll          兼容回退（priority 3）
      okularGenerator_poppler.dll        兼容回退（priority 1）
    mengshee_pdf/
      MengsheePdfEngineMuPdf.dll
      MengsheePdfEnginePoppler.dll
    kf6/…                                okularpart、KIO worker 等
    kiconthemes6/…
share/
  poppler/…                              CMap/CID 数据（两个引擎共用）
  licenses/mupdf/COPYING
  licenses/poppler/COPYING
  licenses/poppler/COPYING3              （源码存在时）
  licenses/tesseract/LICENSE             （工作区源码存在时，尽力而为）
  licenses/leptonica/leptonica-license.txt
  licenses/poppler-data/COPYING*         （尽力而为）
```

插件的默认搜索路径（见 `generators/pdf/pdfengineregistry.cpp`）是：

1. `MENGSHEE_PDF_ENGINE_PATH`（目录列表，测试/开发用）；
2. `<mengshee.exe 目录>\plugins\mengshee_pdf`；
3. `<mengshee.exe 目录>\mengshee_pdf`。

因此引擎模块必须位于 `bin\plugins\mengshee_pdf\`。默认未设置
`MENGSHEE_PDF_ENGINE` 时按 MuPDF、Poppler 的顺序选择第一个可用模块。

模块文件名必须遵循 `MengsheePdfEngine<Id>.dll`，其中 `<Id>` 转小写后就是 engine id
（`MengsheePdfEngineMuPdf.dll` → `mupdf`，`MengsheePdfEnginePoppler.dll` →
`poppler`）。发现是惰性的：

- `availableEngines()` / `hasEngine()` / `engineInfo()` 只扫描目录并按文件名记录
  id、displayName 和路径，**不加载 DLL**；displayName 对 `mupdf` / `poppler`
  固定为 `MuPDF` / `Poppler`，其它引擎使用 id；
- `createEngine(id)` 只 `QLibrary` 加载被请求的那一个模块，并校验模块 descriptor
  的 id 与请求 id 一致；不一致时报错并丢弃（卸载）该模块；
- `createSelectedEngine()` 只加载最终选中的那一个引擎；`MENGSHEE_PDF_ENGINE`
  仍可指定一个显式模块路径，该路径会被直接加载。

## 2.1 选择默认引擎

单插件按以下优先级选择引擎：

1. `MENGSHEE_PDF_ENGINE`（测试/开发者覆盖，可填 id 或模块路径）；
2. `[Core General] PdfEngine` 用户配置（`MuPDF` 或 `Poppler`，大小写不敏感）；
3. 可用模块中的 MuPDF、Poppler 顺序。

用户配置示例（`mengshee` 共享配置）：

```ini
[Core General]
PdfEngine=Poppler
```

修改后需要重新打开文档（引擎在文档打开时选择）。设置界面在
“设置 → General → PDF engine” 提供同一个选项（MuPDF / Poppler）。

## 3. 代码改动清单

| 文件 | 改动 |
| --- | --- |
| `CMakeLists.txt` | 新增 `MENGSHEE_PDF_ENGINE_MODULES`（默认 `OFF`）；在 `add_subdirectory(generators)` 之后安装单插件、MuPDF 引擎模块（`TARGETS`）和 Poppler 引擎模块副本（`install(FILES)`）。旧插件 install 规则不动。 |
| `windows-build/cmake/pdf-engine-modules.cmake` | 新增共享解析函数：开关归一化、引擎模块/旧插件/运行时 DLL/单插件的 build-or-install 路径解析、缺树时的可操作报错。 |
| `windows-build/cmake/build-windows.cmake` | 开关默认值与 configure 参数；deploy 前双树预检；向 deploy/package 传递开关和可选的 engine root。 |
| `windows-build/cmake/deploy-runtime.cmake` | phase-2 分支：解析两棵树，复制单插件和两个模块到 `bin\plugins\…`，同时保留两个旧插件；不再过滤 Poppler SDK DLL、CMap 数据和 MuPDF/Poppler 许可证；`OFF` 时显式清掉阶段中的 phase-2 残留。 |
| `windows-build/cmake/package-windows.cmake` | 显式同步 `share\licenses`；按舞台形状校验：双引擎舞台要求单插件、两模块、两套依赖、两个旧插件和两份引擎许可证；部分 phase-2 布局直接失败。 |
| `windows-build/installer/mengshee-installer.iss` | 升级清理条件改为按“舞台是否带 `poppler.dll`/单插件”判断，避免双引擎包误删 Poppler，也避免回退到 legacy 后残留高优先级单插件。 |
| `windows-build/manifests/required-components.txt` | 补充单插件、两个引擎模块、`MengsheePdfRuntime.dll`、Tesseract/Leptonica、两份引擎许可证；旧 Poppler 清单注释改为按部署形状解释。 |
| `windows-build/scripts/smoke-test-mengshee-stage.ps1` | 自动识别双引擎舞台；新增 `-Engines auto|both|mupdf|poppler`；默认在双引擎舞台上分别以 `MENGSHEE_PDF_ENGINE=mupdf` 和 `=poppler` 启动两次，每遍校验“仅所选引擎模块”及其依赖已加载、另一个引擎模块未加载且未回退旧插件；legacy 舞台保持原检查。 |

部署相关的改动没有修改 `generators/mupdf/**`、`generators/poppler/**` 的源码或
CMake；`generators/pdf/**` 的 CMake 也未改。之后 `generators/pdf/pdfengineregistry.*`
和 `generators/pdf/autotests/registrytest.cpp` 加入了第 2 节描述的惰性发现/校验。

## 4. 依赖与许可证

| 组件 | 用途 | 许可证 | 随包文件 |
| --- | --- | --- | --- |
| MuPDF | MuPDF 引擎（静态链接进 `MengsheePdfRuntime.dll`） | AGPL-3.0-or-later | `share/licenses/mupdf/COPYING` |
| Poppler | Poppler 引擎（`poppler.dll`、`poppler-qt6.dll`） | GPL-2.0-or-later | `share/licenses/poppler/COPYING`、`COPYING3` |
| poppler-data | CMap/CID 数据（`share/poppler`） | cMap 数据按 Adobe 许可；cidToUnicode/nameToUnicode/unicodeMap 为 GPLv2/v3 | `share/licenses/poppler-data/COPYING*`（工作区有构建缓存时） |
| Tesseract | Poppler 引擎的 OCR | Apache-2.0 | `share/licenses/tesseract/LICENSE`（工作区源码存在时） |
| Leptonica | Tesseract 的图像处理 | BSD-2-Clause | `share/licenses/leptonica/leptonica-license.txt` |
| Qt / KF6 / MSVC runtime | 应用与插件运行时 | 各自许可证 | 现有发行流程处理 |

注意：

- MuPDF 模块的 Windows 归档静态嵌入了 Tesseract；Poppler 路径则使用 SDK 的
  `tesseract55.dll` / `leptonica-1.87.0.dll`，两者互不冲突，但会同时出现在双引擎
  运行时中。
- Poppler 还会间接加载 NSS、libcurl、gpgme 等 DLL。这些传递依赖的许可证文本目前
  仍未随包收集，是本阶段已知的合规缺口；新增的 `share/licenses/poppler*` 只覆盖
  Poppler 本体和 CMap 数据。
- Tesseract/Leptonica/poppler-data 的许可证文本拷贝是“找到就复制”的尽力行为；
  源码缓存被清理时 deploy 会打印 warning，不阻断部署。正式发布前应在完整工作区
  构建一次并确认 `share/licenses` 内容完整。

## 5. 验证

### 5.1 静态检查（不写 install/stage）

```powershell
# 根 CMake 的 phase-2 install 规则：配置应成功
cmake -S . -B windows-build-workspace/tmp/config-check-phase2 -G Ninja `
  -DMENGSHEE_PDF_BACKEND=MuPDF -DMENGSHEE_PDF_ENGINE_MODULES=ON `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ...
# 生成的 install 脚本中应出现：
#   bin/plugins/okular_generators/okularGenerator_mengshee_pdf.dll
#   bin/plugins/mengshee_pdf/MengsheePdfEngineMuPdf.dll
```

### 5.2 舞台文件

```powershell
$stage = "C:\Users\jairy\Documents\mengshee\windows-build-workspace\dist\mengshee-pdf\app"
Test-Path "$stage\bin\plugins\okular_generators\okularGenerator_mengshee_pdf.dll"
Test-Path "$stage\bin\plugins\mengshee_pdf\MengsheePdfEngineMuPdf.dll"
Test-Path "$stage\bin\plugins\mengshee_pdf\MengsheePdfEnginePoppler.dll"
Test-Path "$stage\bin\MengsheePdfRuntime.dll"
Test-Path "$stage\bin\poppler.dll"
Test-Path "$stage\bin\poppler-qt6.dll"
Test-Path "$stage\bin\tesseract55.dll"
Test-Path "$stage\bin\leptonica-1.87.0.dll"
Test-Path "$stage\share\poppler\cMap\Adobe-GB1\UniGB-UTF16-H"
Test-Path "$stage\share\licenses\mupdf\COPYING"
Test-Path "$stage\share\licenses\poppler\COPYING"
# 兼容回退仍然存在
Test-Path "$stage\bin\plugins\okular_generators\okularGenerator_mupdf.dll"
Test-Path "$stage\bin\plugins\okular_generators\okularGenerator_poppler.dll"
```

### 5.3 运行 smoke test（分别验证两个引擎）

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile `
  -File .\windows-build\scripts\smoke-test-mengshee-stage.ps1 `
  -PdfPath .\autotests\data\file2.pdf `
  -Engines both
```

在双引擎舞台上：

- 引擎发现是惰性的：注册表扫描只读取模块文件名（`MengsheePdfEngine<Id>.dll`），
  不会加载 DLL。因此每一遍只要求单插件、**所选**引擎模块及其依赖已加载：
  `mupdf` 那遍要求 `MengsheePdfEngineMuPdf.dll` 和 `MengsheePdfRuntime.dll`，
  `poppler` 那遍要求 `MengsheePdfEnginePoppler.dll`、`poppler.dll` 和
  `poppler-qt6.dll`；
- 第一遍设置 `MENGSHEE_PDF_ENGINE=mupdf`，第二遍设置 `=poppler`，分别走一次
  引擎创建/打开/渲染路径；
- 每一遍都会检查**未选中的那个引擎模块没有进入进程**（这正是“只加载所选模块”的
  回归检查；如果发现逻辑退化成全量加载，smoke test 会失败）；
- 每一遍都检查进程没有回退去加载旧插件（`okularGenerator_mupdf.dll` /
  `okularGenerator_poppler.dll`）。如果所选引擎创建失败，Okular 会加载旧插件，
  该检查会失败；
- 两遍都会检查是否从 stage 之外加载了 workspace 模块。

只想验证一个引擎时用 `-Engines mupdf` 或 `-Engines poppler`；`-Engines auto`
（默认）在双引擎舞台等价于 `both`，在 legacy 舞台只跑 legacy 检查。

### 5.4 打包验证

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DCLEAN_STAGE=ON `
  -DMENGSHEE_PDF_ENGINE_MODULES=ON `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -DSKIP_INSTALLER=ON `
  -P windows-build/cmake/package-windows.cmake
```

phase-2 舞台会校验第 2 节的三个插件/模块、两套运行时依赖、CMap/CID、两个旧插件和
MuPDF/Poppler 许可证；缺少任意一项都会 fatal。legacy 舞台仍走原来的单后端校验。

## 6. 回退方案

### 6.1 回到 legacy 部署（推荐）

1. 用 `-DMENGSHEE_PDF_ENGINE_MODULES=OFF` 重新构建/部署。
   `deploy-runtime.cmake` 会把单插件和 `bin\plugins\mengshee_pdf\` 从阶段插件目录
   移除，只装当前后端的旧插件和依赖。
2. 安装包升级时，`mengshee-installer.iss` 的 `[InstallDelete]` 会在舞台不带
   `okularGenerator_mengshee_pdf.dll` 时删除目标目录中残留的单插件和
   `bin\plugins\mengshee_pdf`；否则高优先级的单插件会在下次启动时继续生效。
3. 如果两个旧插件都保留在目标目录（双引擎阶段 → legacy 阶段），Okular 会按
   priority 3/1 重新选择旧插件；旧插件的依赖（`MengsheePdfRuntime.dll` 或
   `poppler*.dll`）也已部署。

### 6.2 在已安装目录中手动回退

```powershell
$app = "C:\Program Files\Mengshee"
Remove-Item "$app\bin\plugins\okular_generators\okularGenerator_mengshee_pdf.dll"
Remove-Item -Recurse "$app\bin\plugins\mengshee_pdf"
```

之后启动应用即回到旧插件路径。若要保留单插件、只切换引擎，可设置
`MENGSHEE_PDF_ENGINE=mupdf` 或 `MENGSHEE_PDF_ENGINE=poppler`（测试/诊断用，不是
用户设置）。

### 6.3 单个引擎不可用

- Poppler 引擎加载失败（缺 `poppler.dll` 等）时，单插件打开文档会失败，Okular 会
  继续尝试旧插件；这也是 phase-2 阶段必须保留两个旧插件的原因。
- MuPDF 引擎加载失败时同理回退到旧 MuPDF 插件；`MengsheePdfRuntime.dll` 必须与
  模块来自同一棵树。

## 7. 阶段切换点与已知风险

**切换点**：当 `-DMENGSHEE_PDF_ENGINE_MODULES=ON` 的舞台通过 5.2/5.3/5.4 后，把
`windows-build/cmake/build-windows.cmake` 中 `MENGSHEE_PDF_ENGINE_MODULES` 的默认值
从 `OFF` 改为 `ON`（或让 `build-mengshee.cmd` 默认传入
`-DMENGSHEE_PDF_ENGINE_MODULES=ON`），再删除旧插件 install/部署规则。切换前保持
`OFF`。

需要注意的行为变化：

- 单插件 JSON 的 `X-KDE-Priority` 是 5，旧 MuPDF 是 3、旧 Poppler 是 1。只要单插件
  进入 `bin\plugins\okular_generators`，Okular 就会优先选择它；旧插件只作为
  “新插件打开失败”时的回退。也就是说，双引擎部署本身就是运行时切换，不只是
  “多放几个文件”。如果要在切换前完全保持旧路径，必须继续用 `OFF`。
- 单插件和引擎模块必须来自同一源码修订；用旧的 `build\mengshee-standalone` 配新
  的 `build\mengshee-poppler` 可能通过 `apiVersion` 却在行为上不一致。建议每次
  双引擎部署前用同一提交重建两棵树。
- `MENGSHEE_PDF_ENGINE_MODULES=ON` 会让 install 树也带 phase-2 产物；直接运行
  `install\mengshee\bin\mengshee.exe` 时同样优先单插件。想保留 legacy 可运行树，
  用 `OFF` 重新 install/deploy，或从该树删除单插件目录（见 6.2）。
- 安装器 `[InstallDelete]` 的 ISPP 条件已通过 Inno Setup 6 实际编译验证（ISPP probe
  覆盖双引擎 stage 与 legacy 回退两种条件）；真实安装/升级/卸载仍未在用户机器上执行。
- 已在独立临时工作区完成一次完整验证（2026-11-16）：两棵树分别
  `cmake --install` 到全新前缀；`MENGSHEE_PDF_ENGINE_MODULES=ON` deploy 出单插件、
  两模块、两套依赖和许可证；package 成功；双引擎 smoke 打开 `file2.pdf`，每遍只
  加载所选引擎且未回退旧插件；Inno Setup 6 编译成功。验证中发现并修复了 Poppler
  辅助树在非规范 `BUILD_DIR` 下缺少 `MENGSHEE_POPPLER_PRIVATE_BUILD_DIR` 的 configure
  问题。
- 仍需注意：安装器真实升级/卸载、`[Run]` 的 vc_redist 流程未实测；正式打包前应从
  同一提交重建两棵树。
- `package-windows.cmake` 用舞台里是否存在单插件/引擎模块来判断形状；如果 install
  树只装了一半 phase-2 产物，打包会明确失败而不是生成混合包。这是有意的。
