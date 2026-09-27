# 网页截图说明

已采用用户陆续提供的六张真实截图，其中两张分别展示在 Sumatra PDF 和 Adobe Acrobat 中打开批注文件的效果。没有生成、拼接、裁切或重绘应用界面；网页按比例显示，点击可在新标签页查看完整原图。

| 文件 | 原始尺寸 | 页面用途 |
| --- | --- | --- |
| `assets/reference-panes.png` | 1664 × 866 | 首屏：目录链接、目标标记与辅助阅读窗格 |
| `assets/latex-note.png` | 956 × 620 | LaTeX 批注：定理旁的整数符号批注与源码编辑窗 |
| `assets/proofreading.png` | 1360 × 925 | 校稿：两个编号引出标注、内部 ID、LaTeX 源码与公式 |
| `assets/reading-views.png` | 1651 × 1107 | 按 View 阅读：摘要和图示区域、后续正文区域以及可见批注 |
| `assets/sumatra-portability.png` | 1281 × 1071 | PDF 兼容性：在 Sumatra PDF 中显示相同的编号标注和公式 |
| `assets/acrobat-portability.png` | 1916 × 1108 | PDF 兼容性：在 Adobe Acrobat 中显示相同的编号标注和公式 |

## 取舍

- 第一张宽图放在首屏，不在交叉引用部分重复，避免同一张图反复出现。
- LaTeX 部分使用用户后来提供的数学示例：定理中高亮的 “any integer m”、渲染后的整数符号批注及对应源码。替换了最初的文献笔记截图。
- 校稿图注明确这些是演示批注，不代表原文存在错误。
- Views 使用后来提供的真实阅读截图，展示不同宽度的阅读区域与保留的批注。
- Sumatra PDF 与 Adobe Acrobat 图并列放在兼容性部分，分别标明阅读器名称，不将其描述成扁平化导出效果。
- 图片适度缩小显示：首屏最大宽 960px，LaTeX 图 500px，校稿和 Views 图 780px；兼容性对照区最大宽 1050px，桌面双栏，小屏单栏，均可点击原图。
- 原图中包含批注作者显示名、时间和文献内容；未擅自删改。公开部署前请确认这些信息及文献截图可以公开使用。

Views 图中也保留了原图可见的机构下载标记；上线前一并确认可以公开。

## 来源校验

文件直接复制自用户上传附件，SHA-256：

- `acrobat-portability.png`: `2c4bee64c84153e968dca46540d8a7c10c540c360bae5c2275d782b232a171cd`

- `sumatra-portability.png`: `7a74ee5bebf3b3f5ec27829d39a156bbf757f17205ea73a8ace105a3ef192a5b`

- `reading-views.png`: `844e380ebe78cebac601a79a6651accb4c11342394737be3f31d0b0da1dcd48d`

- `reference-panes.png`: `98df9721ce7cabf478a9b064d63746b50156c259cb9e23d68044b095c00e3261`
- `latex-note.png`: `6e906aee6539d2f512389d8bac9ba256fa123838601b1dd32bc0a5624c5d58ce`
- `proofreading.png`: `23551e97358396a95160b0c07679b3dcd233d7a03344ddae297036b48083fd6e`
