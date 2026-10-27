---
title: 编写 LaTeX 批注
sidebar:
  order: 4
---

## 新建批注

选择 **LaTeX Note（LaTeX 批注）**，在段落旁放置批注框。新批注以 `\LaTeX` 作为可编辑的示例内容。将它替换为自己的说明，等待渲染完成，确认结果后保存 PDF。如果需要指向精确位置，可使用 LaTeX 引出式批注。

完成下方的中文字体配置后，可以试用这段源码：

```latex
这个投影算符的像为
$\operatorname{im}P(q_0)=\{P(q_0)v:\ v\in\mathcal H_e\}$。
```

`$…$` 用于行内公式，`$$…$$` 用于独立显示的公式。公式前后可以写普通文字。可用的字体和宏包取决于安装的 StemTeX 配置。

## 配置 LaTeX 排版环境

从设置菜单打开 **Configure Annotations…（配置批注）**，或在软件设置中进入 **Annotations（批注）** 页面，找到 **LaTeX Notes** 区域。这些选项决定渲染环境；单个批注的颜色、边框和字号通过批注属性设置。

| 设置项 | 用途 |
| --- | --- |
| **StemTeX profile（配置）** | 决定排版使用的字体、已加载宏包和导言区。**Auto (recommended bundled profile)** 自动选择推荐的内置配置；列表用 **(bundled)** 和 **(user)** 区分内置配置和用户配置。 |
| **Create…（创建）** | 打开 StemTeX Profile Creator，根据选定的 TeX 资源树创建用户配置。 |
| **Open Folder（打开文件夹）** | 打开 Mengshee 的用户配置目录。请用此按钮定位，不要与独立 StemTeX 程序的配置目录混淆。 |
| **StemTeX TeX tree（TeX 资源树）** | 指定宏包和字体来源。留空使用随程序提供的资源树，也可以按下文选择本地 TeX Live。 |
| **StemTeX status（状态）** | 显示引擎是否就绪以及渲染进度；初始化失败时请查看旁边的说明。 |

### 使用内置环境

如果只写拉丁文字和基本数学公式，可以将 **StemTeX TeX tree** 留空，并将配置设为 **Auto**。内置 `unicodemath` 配置采用 Latin Modern 正文字体和数学字体，并加载 `unicode-math`，无需另外安装完整 TeX Live。

内置资源树经过精简，不包含中日韩文字支持，也不包含完整的可选宏包集合。**需要写中文时，应选择合适的本地 TeX Live 资源树，并创建选有 CJK 字体的配置。** 仅在 Windows 中安装中文字体还不够，因为排版还需要 `xeCJK` 等宏包。

### 使用本地 TeX Live

1. 在本机安装 TeX Live，并确保包含需要使用的宏包和字体。
2. 在 **StemTeX TeX tree** 中浏览选择**包含 `texmf-dist` 的目录**。例如存在 `C:\texlive\2026\texmf-dist\web2c` 时，应选择 **`C:\texlive\2026`**，具体路径和年份以你的安装位置为准。
3. 不要选择 `texmf-dist` 本身、`bin` 目录或 `xelatex.exe`。这里填写的是资源树，不是可执行程序。
4. 点击 **Create…**，为该资源树创建配置，然后在 Mengshee 中选中生成的用户配置，点击**应用**或**确定**保存设置。
5. 新建一条简短批注测试，先试普通文字和 `$x^2$`，再加入中文或需要特定宏包的命令。

选择本地 TeX Live 后，Mengshee **仍使用 StemTeX 自己的引擎和转换器**，只是从本地 TeX Live 读取宏包和字体，并不是改为调用系统的 `xelatex` 命令。因此仍需保留 StemTeX 支持组件。

### 创建字体与宏包配置

在 Profile Creator 中确认 **TeX Live tree**，填写 **Profile name（配置名称）**，并保留 Mengshee 传入的 **Save to（保存位置）**。

- **Fonts（字体）**：分别选择 **Text font（正文）**、**Math font（数学）**；需要中文时再选择 **CJK font**。可用选项取决于资源树及所需宏包是否齐全。
- **Packages（宏包）**：勾选需要的功能，例如使用 `\ce{H2O}` 时选择 `mhchem`，排版物理量和单位时选择 `siunitx`。可选宏包不会全部自动启用；受支持选项的依赖由创建器补齐。
- **Custom（自定义）**：填写额外导言区命令，例如下方的自定义命令。不要包含 `\documentclass` 或 `\begin{document}` / `\end{document}`。自定义内容所需的宏包仍须存在且兼容。
- **Preamble（导言区）**：检查生成的导言区，然后点击 **Create Profile（创建配置）**。关闭创建器返回 Mengshee 后，配置列表会刷新。如果只新增了一份配置，程序会自动选中它；否则需要手动选择，最后应用设置。

```latex
\newcommand{\R}{\mathbb{R}}
```

选中这个配置后，批注中就可以写 `$x\in\R$`。宏包加载和共用定义放在配置中；批注编辑器中填写实际要显示的文字与公式。

### 环境问题排查

如果提示资源树不可用，请确认所选目录下存在 `texmf-dist\web2c`。如果 **Create…** 提示找不到 Profile Creator，请修复安装 Mengshee 的 StemTeX 支持组件。出现命令、字体或宏包缺失时，应同时核对资源树与当前配置：宏包已经安装，并不代表已经在批注中加载。

恢复内置环境的方法是：清空 **StemTeX TeX tree**，选择 **Auto**，然后应用。依赖额外宏包或中文支持的旧批注，再次编辑时仍需要相应环境。其他阅读器可以直接显示保存的外观，但 PDF 不会把本机的 TeX Live 或用户配置安装到另一台电脑上。

## 宽度控制换行，高度适应内容

修改批注宽度会改变文字换行。Mengshee 根据渲染内容自动调整高度。批注旋转后，应按其自身排版方向使用宽度控制点；文字基线不一定与屏幕水平方向一致。只修改几个字时，批注原来的方向应保持不变。

![LaTeX 批注及其可编辑源码](/mengshee/assets/latex-note.png)

## 一起保存源码和外观

保存时，可编辑源码和渲染外观都会写入 PDF。其他阅读器无需 StemTeX 就能显示外观，但不一定提供 Mengshee 的源码编辑功能。需要继续修改时应保留可编辑 PDF；扁平化会将可见外观变成页面内容。

如果渲染失败，查看错误信息并检查源码、宏包和运行库是否可用。打印对话框中的纸张方向不会旋转保存的批注，也不会改变其源码。
