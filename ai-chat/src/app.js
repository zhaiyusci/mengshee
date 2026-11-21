// The chat window of the application. Mengshee started this window and talks
// to it over standard input/output; the document, page text, page images,
// model answers and page jumps all arrive through that channel.

const tauri = window.__TAURI__;
const invoke = tauri?.core?.invoke;

const elements = {
    title: document.getElementById("document-title"),
    id: document.getElementById("document-id"),
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
    pageBlocks: new Map(),
    history: [],
    context: null,
    busy: false,
    attachments: [],
    useImages: localStorage.getItem("mengshee-ai-use-images") !== "0",
    usage: { prompt: 0, completion: 0, cached: 0, total: 0 }
};

const maxImagePages = 16;

const systemPrompt = `You answer questions about one PDF. The first user message contains indexed source text lines,
grouped by physical PDF page, optionally alongside the corresponding full-page images.
Each source line is written as an exact block ID, a tab, and its text. The reader already knows its coordinates.
Use only that document; never use outside knowledge. If it does not contain the answer, say so plainly.
Answer in the language of the question. Be concrete and direct; do not explain your reasoning.

Format: Markdown prose. Support each claim with an inclusive source range:
[evidence description](cite:START_ID..END_ID)
Use the exact first and last block IDs of the complete evidence passage. The reader includes EVERY
intervening block in the supplied reading order, including continuation into the next column.
For one block, use the same ID as both endpoints. Never enumerate individual lines of one passage.
For genuinely separate passages, use separate links or comma-separated ranges in one link:
[evidence description](cite:START_ID_1..END_ID_1,START_ID_2..END_ID_2)
Replace these placeholders with exact IDs from the supplied index. Never invent IDs or coordinates.
Choose complete sentences, including introductory context and continuation across line breaks.
Use separate ranges across page headers/footers or unrelated material; do not omit a line inside a sentence.
Do not cite a header or footer unless it actually provides the evidence. Link text may describe the evidence;
it is never used to search the PDF. If evidence is visible in an image but has no indexed text line,
use [第 N 页](pdf:N) and explain that only page-level navigation is available. N is the physical page index,
not the printed page label. If evidence is absent, say so and do not invent a citation.
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
        updateDocument(result.document?.pages ? result.document : null);
        return true;
    } catch {
        state.document = null;
        renderDocumentState();
        return false;
    }
}

function documentKey(document) {
    return JSON.stringify(document && [document.id, document.path, document.pages, document.revision]);
}

function updateDocument(next) {
    if (documentKey(next) !== documentKey(state.document)) {
        state.pageBlocks.clear();
        state.context = null;
        if (state.history.length) {
            appendMessage("assistant", "<p>文档内容已更新，后续问答将使用更新后的文档，重新开始上下文。</p>");
        }
        state.history = [];
    }
    state.document = next;
    renderDocumentState();
}

function documentParams(params = {}) {
    return state.document?.revision === undefined ? params : { ...params, revision: state.document.revision };
}

function renderDocumentState() {
    if (!state.document) {
        elements.title.textContent = "没有打开的文档";
        elements.id.textContent = "";
        elements.meta.textContent = "";
        return;
    }
    elements.title.textContent = state.document.title || state.document.path;
    elements.id.textContent = state.document.id ? `#${state.document.id.slice(0, 8)}` : "";
    elements.meta.textContent = `${state.document.pages} 页 · 当前第 ${state.document.page} 页`;
}

async function documentText() {
    if (!state.document) {
        return "";
    }
    let context = "";
    for (let page = 1; page <= state.document.pages; page += 1) {
        context += await indexedPage(page) + "\n\n";
    }
    return context;
}

async function indexedPage(page) {
    if (!state.pageBlocks.has(page)) {
        const result = await request("pageBlocks", documentParams({ page }));
        if (result.revision !== undefined && result.revision !== state.document?.revision) {
            throw new Error("document-changed");
        }
        state.pageBlocks.set(page, result.blocks || []);
    }
    const blocks = state.pageBlocks.get(page);
    const lines = blocks.map(block => `${block.id}\t${block.text}`).join("\n");
    return `Physical PDF page ${page}:\n${lines || "(No indexed text on this page; page-level citations only.)"}`;
}

async function documentImages() {
    const current = Math.max(1, Math.min(state.document.pages, state.document.page || 1));
    // Send the current page first, then nearby pages. Even a byte-limited
    // request should contain the page the reader is looking at.
    const pages = Array.from({ length: state.document.pages }, (_, i) => i + 1)
        .sort((a, b) => Math.abs(a - current) - Math.abs(b - current) || a - b)
        .slice(0, maxImagePages);
    const content = [];
    const sentPages = [];
    let sentBytes = 0;
    for (const page of pages) {
        const image = await request("pageImage", documentParams({ page, dpi: 140, format: "jpeg" }));
        sentBytes += image.data.length;
        if (sentBytes > 24 * 1024 * 1024) {
            break;
        }
        content.push({ type: "text", text: await indexedPage(page) });
        content.push({ type: "image_url", image_url: { url: `data:${image.mime};base64,${image.data}` } });
        sentPages.push(page);
    }
    if (!sentPages.length) {
        throw new Error("页面图片超过大小限制，请改用文本模式。");
    }
    const note = `文档共 ${state.document.pages} 页，本次发送 ${sentPages.length} 张页面图片：第 ${sentPages.join("、")} 页。`;
    content.unshift({ type: "text", text: `${note} 每张图片前的「PDF page N:」表示页码，引用时使用 N。` });
    appendMessage("assistant", `<p>${note}</p>`);
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
function renderMarkdown(markdown, revision = state.document?.revision) {
    const linkPattern = /\[([^\]]+)\]\((?:cite:([A-Za-z0-9_-]+(?:\.\.[A-Za-z0-9_-]+)?(?:,[A-Za-z0-9_-]+(?:\.\.[A-Za-z0-9_-]+)?)*)|pdf:(\d+))\)/g;
    let html = "";
    let last = 0;
    for (const match of markdown.matchAll(linkPattern)) {
        html += escapeHtml(markdown.slice(last, match.index));
        const destination = match[2] ? `data-ranges="${match[2]}"` : `data-page="${Number(match[3])}"`;
        const version = revision === undefined ? "" : ` data-revision="${escapeHtml(String(revision))}"`;
        html += `<a ${destination}${version}>${escapeHtml(match[1])}</a>`;
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

async function documentContext() {
    const key = JSON.stringify([documentKey(state.document), state.useImages, state.useImages ? state.document?.page : null]);
    if (state.context?.key !== key) {
        const content = state.useImages && state.document ? await documentImages() : await documentText();
        state.context = { key, content };
    }
    return state.context.content;
}

async function runAsk(question, attachments = []) {
    state.busy = true;
    elements.useImages.disabled = true;
    elements.status.textContent = "读取文档…";
    try {
        // Refresh before every question; polling alone can miss a recent edit.
        const result = await request("state");
        updateDocument(result.document?.pages ? result.document : null);
        if (!state.document) throw new Error("没有打开的文档。");
        const images = attachmentImages(attachments);
        const pending = { role: "user", content: images.length ? [{ type: "text", text: question }, ...images] : question };
        const history = [...state.history, pending];
        const hasAttachments = history.some(message => Array.isArray(message.content) && message.content.some(item => item.type === "image_url"));
        let payload;
        for (let attempt = 0; attempt < 2; attempt += 1) {
            const content = await documentContext();
            elements.status.textContent = "生成回答…";
            try {
                payload = await request("ask", documentParams({
                    messages: [{ role: "system", content: systemPrompt }, { role: "user", content }, ...history]
                }));
                break;
            } catch (error) {
                // Document images live outside conversation history, so a
                // text retry cannot accidentally keep images from older turns.
                if (attempt || !state.useImages || hasAttachments || !/image|multimodal|content|400/i.test(String(error.message || error))) throw error;
                state.useImages = false;
                state.context = null;
                elements.useImages.checked = false;
                localStorage.setItem("mengshee-ai-use-images", "0");
                appendMessage("assistant", "<p>该模型不接受页面图片，已改用文本重试。</p>");
            }
        }
        const answer = payload?.choices?.[0]?.message?.content?.trim();
        if (!answer) {
            throw new Error(payload?.error?.message || "模型没有返回内容");
        }
        state.history = [...history, { role: "assistant", content: answer }];
        const node = appendMessage("assistant", renderMarkdown(answer));
        try {
            await typeset(node);
        } catch {
            // Presentation errors must never remove a successful model turn.
            const warning = document.createElement("div");
            warning.className = "usage";
            warning.textContent = "公式排版失败，回答已保留，可查看原始公式。";
            node.append(warning);
        }
        if (payload?.choices?.[0]?.finish_reason === "length") {
            const warning = document.createElement("div");
            warning.className = "usage";
            warning.textContent = "回答达到输出长度限制，内容尚未完整。可以继续追问。";
            node.append(warning);
        }

        const usage = usageFrom(payload);
        if (usage.prompt || usage.completion) {
            const line = document.createElement("div");
            line.className = "usage";
            line.textContent = describeUsage(usage);
            node.append(line);
            accumulateUsage(usage);
        }
        elements.status.textContent = "";
    } catch (error) {
        const message = String(error.message || error);
        if (message === "document-changed") {
            state.pageBlocks.clear();
            state.context = null;
            state.history = [];
        }
        appendMessage(
            "error",
            escapeHtml(
                message === "document-changed"
                    ? "文档在问答过程中发生变化，请重新提问。"
                    : message === "ai-not-configured"
                    ? "请先在 Mengshee 的“通用 AI”设置里配置模型和 API 地址。"
                    : attachments.length && /image|multimodal|400/i.test(message)
                      ? "该模型不接受图片输入，请移除附件后再试。"
                      : message
            )
        );
        elements.status.textContent = "";
    } finally {
        state.busy = false;
        elements.useImages.disabled = false;
    }
}

// The citation contains source IDs, not text to search or repair.
elements.chat.addEventListener("click", async (event) => {
    const link = event.target.closest("a[data-ranges], a[data-block-ids], a[data-page]");
    if (!link) {
        return;
    }
    try {
        const params = link.dataset.ranges !== undefined
            ? { ranges: link.dataset.ranges.split(",").map(range => {
                const [startId, endId = startId] = range.split("..");
                return { startId, endId };
            }) }
            : link.dataset.blockIds !== undefined
            ? { blockIds: [...new Set(link.dataset.blockIds.split(","))] }
            : { page: Number(link.dataset.page) };
        if (link.dataset.revision !== undefined) params.revision = link.dataset.revision;
        const result = await request("jump", params);
        elements.status.textContent = result.located
            ? `已跳到第 ${result.page} 页并高亮${result.pages > 1 ? `（共 ${result.pages} 页证据）` : ""}`
            : `已跳到第 ${result.page} 页（此引用没有文字高亮）`;
    } catch (error) {
        const message = String(error.message || error);
        elements.status.textContent = message === "document-changed"
            ? "文档已更新，这条引用已失效，请重新提问。"
            : message === "citation-range-invalid"
              ? "这条引用的起止范围无效，请重新提问。"
            : message === "citation-unavailable"
              ? "无法定位这条引用：证据编号无效或已失效，请重新提问。"
              : message;
    }
});

function resizeQuestion() {
    elements.question.style.height = "auto";
    elements.question.style.height = `${Math.min(Math.max(elements.question.scrollHeight, 44), 200)}px`;
}

elements.send.addEventListener("click", ask);
elements.question.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !event.shiftKey && !event.isComposing && event.keyCode !== 229) {
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
        // An ask may have started while this polling request was in flight.
        if (state.busy) return;
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
        updateDocument(next);
    } catch {
        missingDocumentCount += 1;
        if (missingDocumentCount >= 5) {
            closeWindow();
        }
    }
}, 1500);
