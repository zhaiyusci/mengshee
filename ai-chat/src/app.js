// The chat window of the application. Mengshee started this window and talks
// to it over standard input/output; the document, page text, page images,
// model answers and page jumps all arrive through that channel.

const tauri = window.__TAURI__;
const invoke = tauri?.core?.invoke;

const elements = {
    title: document.getElementById("document-title"),
    meta: document.getElementById("document-meta"),
    usage: document.getElementById("usage"),
    chat: document.getElementById("chat"),
    question: document.getElementById("question"),
    send: document.getElementById("send"),
    status: document.getElementById("status"),
    useImages: document.getElementById("use-images"),
    attach: document.getElementById("attach"),
    file: document.getElementById("file"),
    attachments: document.getElementById("attachments")
};

const state = {
    document: null,
    pageTexts: new Map(),
    history: [],
    busy: false,
    attachments: [],
    useImages: localStorage.getItem("mengshee-ai-use-images") === "1",
    usage: { prompt: 0, completion: 0, cached: 0, total: 0 }
};

const maxImagePages = 16;

const systemPrompt = `You answer questions about one PDF. The first user message contains that document page by page:
text as "[page N] text", or, when the user chose images, images preceded by "PDF page N:".
Use only that document; never use outside knowledge. If it does not contain the answer, say so plainly.
Answer in the language of the question. Be concrete and direct; do not explain your reasoning.

Format: Markdown prose. Support each claim with an inline link to the document:
[three or more words copied exactly](pdf:12)
The link text must be copied character for character from page 12, so the reader can find it
and highlight it. Do not paraphrase inside the link text.
If the page also prints a label for that object, add it in quotes:
[the counterpoise correction is](pdf:7 "(12)")
Write formulas as ASCII LaTeX: inline $...$, display $$...$$. Never paste Unicode math letters.
Follow-up questions continue the same conversation.`;

async function request(method, params = {}) {
    if (!invoke) {
        throw new Error("这个窗口由 Mengshee 启动。");
    }
    return invoke("bridge_request", { method, params });
}

function formatNumber(value) {
    return new Intl.NumberFormat("zh-CN").format(value || 0);
}

// Providers report usage differently; cover the common names and the cache
// counters of DeepSeek, Kimi and OpenAI-style services.
function usageFrom(payload) {
    const usage = payload?.usage || {};
    const prompt = usage.prompt_tokens ?? usage.input_tokens ?? 0;
    const completion = usage.completion_tokens ?? usage.output_tokens ?? 0;
    const total = usage.total_tokens ?? prompt + completion;
    const details = usage.prompt_tokens_details || usage.input_tokens_details || {};
    const cached = usage.prompt_cache_hit_tokens ?? usage.cache_hit_tokens ?? usage.cached_tokens ?? details.cached_tokens ?? 0;
    return { prompt, completion, total, cached };
}

function describeUsage(usage) {
    const parts = [`输入 ${formatNumber(usage.prompt)}`, `输出 ${formatNumber(usage.completion)}`];
    if (usage.cached > 0) {
        parts.push(`缓存命中 ${formatNumber(usage.cached)}（${Math.round((usage.cached / Math.max(usage.prompt, 1)) * 100)}%）`);
    }
    return parts.join(" · ");
}

function accumulateUsage(usage) {
    state.usage.prompt += usage.prompt;
    state.usage.completion += usage.completion;
    state.usage.cached += usage.cached;
    state.usage.total += usage.total;
    if (!state.usage.prompt && !state.usage.completion) {
        elements.usage.textContent = "";
        return;
    }
    const rate = Math.round((state.usage.cached / Math.max(state.usage.prompt, 1)) * 100);
    elements.usage.textContent = `本次会话 输入 ${formatNumber(state.usage.prompt)} · 缓存 ${formatNumber(state.usage.cached)}（${rate}%）· 输出 ${formatNumber(state.usage.completion)}`;
}

async function connect() {
    try {
        const result = await request("state");
        state.document = result.document?.pages ? result.document : null;
        renderDocumentState();
        return true;
    } catch {
        state.document = null;
        renderDocumentState();
        return false;
    }
}

function renderDocumentState() {
    if (!state.document) {
        elements.title.textContent = "没有打开的文档";
        elements.meta.textContent = "";
        return;
    }
    elements.title.textContent = state.document.title || state.document.path;
    elements.meta.textContent = `${state.document.pages} 页 · 当前第 ${state.document.page} 页`;
}

async function documentText() {
    if (!state.document) {
        return "";
    }
    // The whole document is sent, so page texts are cached after the first ask.
    for (let page = 1; page <= state.document.pages; page += 1) {
        if (state.pageTexts.has(page)) {
            continue;
        }
        const result = await request("pageText", { page });
        state.pageTexts.set(page, result.text || "");
    }
    let context = "";
    for (let page = 1; page <= state.document.pages; page += 1) {
        context += `[page ${page}]\n${state.pageTexts.get(page)}\n\n`;
    }
    return context;
}

async function documentImages() {
    const pages = Math.min(state.document.pages, maxImagePages);
    const note = state.document.pages > pages
        ? `（文档共 ${state.document.pages} 页，这里只发送前 ${pages} 页）`
        : "";
    const content = [{
        type: "text",
        text: `下面 ${pages} 张图片是一份 PDF 的页面。每页前面有一行「PDF page N:」，紧跟的图片就是该页，引用时使用 N。${note}`
    }];
    let sentBytes = 0;
    for (let page = 1; page <= pages; page += 1) {
        const image = await request("pageImage", { page, dpi: 140, format: "jpeg" });
        sentBytes += image.data.length;
        if (sentBytes > 24 * 1024 * 1024) {
            break;
        }
        content.push({ type: "text", text: `PDF page ${page}:` });
        content.push({ type: "image_url", image_url: { url: `data:${image.mime};base64,${image.data}` } });
    }
    return content;
}

// Attachments are downscaled once, so a pasted screenshot does not cost a
// megabyte of tokens and the request stays small.
async function imageAttachment(file) {
    const bitmap = await createImageBitmap(file);
    const scale = Math.min(1, 1600 / Math.max(bitmap.width, bitmap.height));
    const canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.round(bitmap.width * scale));
    canvas.height = Math.max(1, Math.round(bitmap.height * scale));
    canvas.getContext("2d").drawImage(bitmap, 0, 0, canvas.width, canvas.height);
    bitmap.close?.();
    return { url: canvas.toDataURL("image/jpeg", 0.85), name: file.name || "image" };
}

async function addAttachment(file) {
    if (!file || !file.type.startsWith("image/") || state.attachments.length >= 4) {
        return;
    }
    try {
        state.attachments.push(await imageAttachment(file));
        renderAttachments();
    } catch {
        elements.status.textContent = "无法读取这张图片。";
    }
}

function renderAttachments() {
    elements.attachments.replaceChildren();
    for (const [index, attachment] of state.attachments.entries()) {
        const holder = document.createElement("div");
        holder.className = "attachment";
        const image = document.createElement("img");
        image.src = attachment.url;
        image.alt = attachment.name;
        const remove = document.createElement("button");
        remove.textContent = "×";
        remove.title = "移除";
        remove.addEventListener("click", () => {
            state.attachments.splice(index, 1);
            renderAttachments();
        });
        holder.append(image, remove);
        elements.attachments.append(holder);
    }
}

function attachmentImages(attachments) {
    return attachments.map((attachment) => ({ type: "image_url", image_url: { url: attachment.url } }));
}

function escapeHtml(text) {
    return text.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;").replaceAll('"', "&quot;");
}

// Markdown-lite: enough for answers, then MathJax renders the formulas.
function renderMarkdown(markdown) {
    const linkPattern = /\[([^\]]+)\]\(pdf:(\d+)(?:\s+"([^"]*)")?\)/g;
    let html = "";
    let last = 0;
    for (const match of markdown.matchAll(linkPattern)) {
        html += escapeHtml(markdown.slice(last, match.index));
        const page = Number(match[2]);
        const label = match[3] ? match[3] : "";
        html += `<a data-page="${page}" data-label="${escapeHtml(label)}">${escapeHtml(match[1])}</a>`;
        last = match.index + match[0].length;
    }
    html += escapeHtml(markdown.slice(last));
    html = html
        .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
        .replace(/`([^`]+)`/g, "<code>$1</code>")
        .replace(/^#{1,6}\s*(.+)$/gm, "<strong>$1</strong>")
        .replace(/^\s*[-*]\s+/gm, "• ");
    return html
        .split(/\n{2,}/)
        .map((block) => `<p>${block.replaceAll("\n", "<br>")}</p>`)
        .join("");
}

function appendMessage(role, html) {
    const node = document.createElement("div");
    node.className = `message ${role}`;
    node.innerHTML = html;
    elements.chat.append(node);
    elements.chat.scrollTop = elements.chat.scrollHeight;
    return node;
}

async function typeset(node) {
    if (window.MathJax?.typesetPromise) {
        await window.MathJax.typesetPromise([node]);
    }
}

async function ask() {
    const question = elements.question.value.trim();
    if ((!question && !state.attachments.length) || state.busy) {
        return;
    }
    const attachments = state.attachments;
    state.attachments = [];
    renderAttachments();
    elements.question.value = "";
    resizeQuestion();
    const pictures = attachments.map((attachment) => `<img class="inline-image" src="${attachment.url}" alt="">`).join("");
    appendMessage("user", escapeHtml(question).replaceAll("\n", "<br>") + pictures);
    await runAsk(question || "请描述这张图片。", attachments);
}

async function firstContent(question, attachments) {
    if (state.useImages && state.document) {
        const content = await documentImages();
        content.push({ type: "text", text: `Question: ${question}` });
        content.push(...attachmentImages(attachments));
        return content;
    }
    const text = `${await documentText()}\n\nQuestion: ${question}`;
    const images = attachmentImages(attachments);
    return images.length ? [{ type: "text", text }, ...images] : text;
}

async function runAsk(question, attachments = [], imagesAllowed = true) {
    state.busy = true;
    elements.status.textContent = "读取文档…";
    try {
        if (!state.history.length) {
            if (state.useImages && imagesAllowed && state.document) {
                elements.status.textContent = "把页面转成图片…";
            }
            state.history.push({ role: "user", content: await firstContent(question, attachments) });
        } else {
            const images = attachmentImages(attachments);
            state.history.push({ role: "user", content: images.length ? [{ type: "text", text: question }, ...images] : question });
        }

        elements.status.textContent = "生成回答…";
        // Mengshee owns the model settings; this window only sends the conversation.
        const payload = await request("ask", {
            messages: [{ role: "system", content: systemPrompt }, ...state.history]
        });
        const answer = payload?.choices?.[0]?.message?.content?.trim();
        if (!answer) {
            throw new Error(payload?.error?.message || "模型没有返回内容");
        }
        state.history.push({ role: "assistant", content: answer });
        const node = appendMessage("assistant", renderMarkdown(answer));
        await typeset(node);

        const usage = usageFrom(payload);
        if (usage.prompt || usage.completion) {
            const line = document.createElement("div");
            line.className = "usage";
            line.textContent = describeUsage(usage);
            node.append(line);
            accumulateUsage(usage);
        }
        elements.status.textContent = "";
        state.busy = false;
    } catch (error) {
        const message = String(error.message || error);
        // Some services do not accept page images; retry once as text rather
        // than failing the question.
        if (imagesAllowed && state.useImages && !attachments.length && /image|multimodal|content|400/i.test(message)) {
            state.history.pop();
            state.useImages = false;
            elements.useImages.checked = false;
            localStorage.setItem("mengshee-ai-use-images", "0");
            appendMessage("assistant", "<p>该模型不接受图片输入，已改用文本重试。</p>");
            state.busy = false;
            await runAsk(question, [], false);
            return;
        }
        appendMessage(
            "error",
            escapeHtml(
                message === "ai-not-configured"
                    ? "请先在 Mengshee 的“通用 AI”设置里配置模型和 API 地址。"
                    : attachments.length && /image|multimodal|400/i.test(message)
                      ? "该模型不接受图片输入，请移除附件后再试。"
                      : message
            )
        );
        state.history.pop();
        elements.status.textContent = "";
        state.busy = false;
    }
}

// Copying text is something a model does reliably, unlike formatting: when no
// anchor candidate can be located, ask it for the shortest verbatim quote.
function parseQuote(content) {
    const match = content.match(/\{[\s\S]*\}/);
    if (!match) {
        return "";
    }
    try {
        const value = JSON.parse(match[0]);
        return String(value.quote || "").trim();
    } catch {
        return "";
    }
}

async function repairAnchor(page, claim) {
    try {
        const pageText = (await request("pageText", { page })).text || "";
        if (!pageText.trim()) {
            return "";
        }
        const payload = await request("ask", {
            messages: [
                {
                    role: "system",
                    content: 'You copy exact text. Reply with JSON only: {"quote":"..."} where quote is the shortest exact substring of the given page text that supports the claim. If nothing supports it, reply {"quote":""}.'
                },
                { role: "user", content: `Page text:\n${pageText}\n\nClaim: ${claim}` }
            ]
        });
        return parseQuote(payload?.choices?.[0]?.message?.content || "");
    } catch {
        return "";
    }
}

// Clicking a citation opens the page in the reader and asks it to highlight
// the named label or anchor.
elements.chat.addEventListener("click", async (event) => {
    const link = event.target.closest("a[data-page]");
    if (!link) {
        return;
    }
    try {
        const params = { page: Number(link.dataset.page) };
        const candidates = [];
        if (link.dataset.label) {
            candidates.push(link.dataset.label);
        }
        const text = (link.textContent || "").trim();
        if (text && text.length <= 80) {
            candidates.push(text);
        }
        const parenthesized = text.match(/\(([^()]{1,24})\)/);
        if (parenthesized) {
            candidates.push(parenthesized[1]);
        }
        const numbered = text.match(/([A-Za-z]?\d+(?:[.\-]\d+)*)/);
        if (numbered) {
            candidates.push(numbered[1]);
        }
        params.anchors = [...new Set(candidates.filter((value) => value && value.trim()))];
        let result = await request("jump", params);
        if (!result.located) {
            elements.status.textContent = "正在定位原文…";
            const quote = await repairAnchor(params.page, text);
            if (quote) {
                result = await request("jump", { page: params.page, anchors: [quote] });
            }
        }
        elements.status.textContent = result.located
            ? `已跳到第 ${params.page} 页并高亮`
            : `已跳到第 ${params.page} 页`;
    } catch (error) {
        elements.status.textContent = String(error.message || error);
    }
});

function resizeQuestion() {
    elements.question.style.height = "auto";
    elements.question.style.height = `${Math.min(Math.max(elements.question.scrollHeight, 44), 200)}px`;
}

elements.send.addEventListener("click", ask);
elements.question.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !event.shiftKey) {
        event.preventDefault();
        ask();
    }
});
elements.question.addEventListener("input", resizeQuestion);
elements.attach.addEventListener("click", () => elements.file.click());
elements.file.addEventListener("change", async () => {
    for (const file of elements.file.files) {
        await addAttachment(file);
    }
    elements.file.value = "";
});
// Pasting a screenshot attaches it, like any other image.
elements.question.addEventListener("paste", async (event) => {
    const files = [...(event.clipboardData?.items || [])]
        .map((item) => item.getAsFile())
        .filter((file) => file && file.type.startsWith("image/"));
    if (files.length) {
        event.preventDefault();
        for (const file of files) {
            await addAttachment(file);
        }
    }
});
elements.useImages.checked = state.useImages;
elements.useImages.addEventListener("change", () => {
    state.useImages = elements.useImages.checked;
    localStorage.setItem("mengshee-ai-use-images", state.useImages ? "1" : "0");
});

connect();

// The window belongs to one document of the running application: when the
// document closes, or the application goes away, the window closes too.
let missingDocumentCount = 0;

function closeWindow() {
    try {
        const current = window.__TAURI__?.window?.getCurrentWindow?.();
        if (current) {
            current.close();
        } else {
            window.close();
        }
    } catch {
        window.close();
    }
}

setInterval(async () => {
    if (state.busy) {
        return;
    }
    try {
        const result = await request("state");
        const next = result.document?.pages ? result.document : null;
        if (!next) {
            missingDocumentCount += 1;
            if (missingDocumentCount >= 5) {
                closeWindow();
                return;
            }
        } else {
            missingDocumentCount = 0;
        }
        if (JSON.stringify(next) !== JSON.stringify(state.document)) {
            state.document = next;
            state.pageTexts.clear();
            renderDocumentState();
        }
    } catch {
        missingDocumentCount += 1;
        if (missingDocumentCount >= 5) {
            closeWindow();
        }
    }
}, 1500);
