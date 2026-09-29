# PDF 字体解释与 Windows 标准字体部署

## 规范边界

依据 [ISO 32000-1:2008](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf)：

- §9.6.2.2：阅读器必须提供 Standard 14 字体，或相应度量及合适的替代字体。这包括 Symbol、ZapfDingbats，不只是普通拉丁文字。
- §9.6.6：简单字体按字符编码及 Encoding/Differences 选择字形；Type 1 缺少编码所指字形时使用 `.notdef`，不能直接把文本提取结果当成另一种绘制指令。
- §9.7：复合字体遵循字符码 → CMap → CID → 字体字形的关系。Identity-H/V 不是“字符码就是 Unicode”的声明。
- §9.10：ToUnicode 提供文本语义/提取映射。保留 Poppler 在字体解析和替代构建阶段的合法 Unicode 辅助映射，但不在绘制阶段另选一个系统字体覆盖既定字形。
- §12.5.5：已有批注 AP 是绘制依据；不能仅在读取时因发现了新的字体或 Contents 与外观不一致，就擅自重新生成外观；用户主动编辑内容时的外观更新是另一回事。

## 本次机制修正

1. 删除 `SplashOutputDev.cc` 中本地添加的 BMP 非 ASCII 字符覆盖绘制，包括后加的 `hasGlyph` 补丁。恢复上游 `font/code` 的填充、描边和裁剪路径，不引入字符白名单。
2. 保留成熟的 `GfxFont` 编码、CMap、CIDToGID 等解析。外部字体定位统一通过 `getExternalFont` 检查实际程序格式，而不是把注册表/后缀推测的所有文件都当作 TrueType；保留 TTC face index。
3. Windows Standard 14 优先使用随包私有 CFF 字体。嵌入字体仍优先，不被这些资源替换。
4. 非标准简单字体在系统匹配失败且没有显式别名时，交回上游按 serif/fixed/bold/italic 属性选择替代的路径，不一律强制 Helvetica。CID 集合匹配和显式 cidfmap 别名保留。
5. 字形边界计算与绘制使用同一字体/字符码，删除旧覆盖绘制机制遗留的“非嵌入非 ASCII 一律拒绝”判断。
6. DLL 相对资源路径使用 Unicode Windows API 转 UTF-8，避免非 ASCII 安装路径导致私有字体失踪。

没有根据某份 PDF、某个希腊字母或截图字号设置特殊分支。

## 可重现字体资源

`external/pdf-base14-fonts/` 固定 PDFium 提交 `a84323421e94f484faca52dd9d027934eba42ab8`，含 14 份原始数组源码、原始许可、来源说明、离线提取工具及校验清单。14 个 CFF 共 264741 字节。

- 上游：[PDFium 字体源码](https://github.com/chromium/pdfium/tree/a84323421e94f484faca52dd9d027934eba42ab8/core/fxge/fontdata/chromefontdata)。各字体源码声明 BSD 风格许可并保留 Foxit 归属；完整上游 LICENSE 原样保留。
- 运行时位置：`<poppler.dll 所在 bin 的父目录>/share/fonts/`。
- SDK、开发运行目录、核心安装包均必须包含 14 字体及 LICENSE/NOTICE/manifest；不是可选 StemTeX 的一部分，不依赖机器安装 TeX 或这些字体。
- `windows-build/cmake/pdf-base14-fonts.cmake` 对源码和部署资源检查哈希，缺少或损坏资源时打包失败。两种 SDK 构建入口都部署同一资源。
- 不分发 Windows 的专有字体，不注册系统字体。

## 验证

2026.0.22.4 的 Windows 验证：

- 字体专项 QtTest 总计 **66 passed / 0 failed / 0 skipped**，含初始化/清理。覆盖字符编码与 ToUnicode 分离、空白字形、全部 14 字体、嵌入 CFF 对照、Symbol/Zapf 字形名及 PDF 指定字宽、12 种缺失字体家族/样式、嵌入 TrueType 的横排/竖排 CIDToGID、Type3、已有 FreeText AP 保存往返。
- 字形边界专项 **4 passed / 0 failed / 0 skipped**，含初始化/清理。
- 完整 staged runtime 重跑相同专项。实际文档第 3、12 页与 SDK 输出逐 RGBA 像素一致；视觉对照 PDFium 确认公式字形恢复预期形态。不同渲染器的抗锯齿不要求逐像素相同。
- 仅复制 DLL、平台插件与私有字体到含中文/空格的新目录，不提供 SDK/TeX 路径；字体来源仍是该目录，渲染像素一致。
- 资源部署测试涵盖缺失/损坏字体和缺失许可的拒绝、路径迁移。独立 staged 应用启动与模块来源检查通过。
- 未运行模式切换、工具栏矩阵等无关测试；未修改用户 PDF 或运行中的安装版。

## 未扩大到的范围

这不是对任意损坏 PDF 或所有字体排版功能的完备性认证。非 Standard 14 且未嵌入的字体仍可能需要系统字体或近似替代。

新建/编辑 FreeText 的 Unicode 排版和字体嵌入是独立的作者侧问题。本次没有改写 `Annot.cc`、`Form.cc` 或 Qt 批注生成逻辑。审计发现其旧的临时字体生成、按第一个非 ASCII 字符选择后续字体、TTC/复杂文字处理仍值得单独整改；本次不声称解决这些问题，也不以跨字体的页面绘制补丁掩盖它们。已有 AP 的读取与保存往返已经专项验证。
