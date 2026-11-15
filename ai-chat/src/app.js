// The chat window of the application. Mengshee started this window and talks
// to it over standard input/output; the document, page text, model answers and
// page jumps all arrive through that channel.

const tauri = window.__TAURI__;
const invoke = tauri?.core?.invoke;

const elements = {
    title: document.getElementById("document-title"),
    meta: document.getElementById("document-meta"),
    chat: document.getElementById("chat"),
    question: document.getElementById("question"),
    send: document.getElementById("send"),
    status: document.getElementById("status")
};

const state = {
    document: null,
    pageTexts: new Map(),
    history: [],
    busy: false
};

const systemPrompt = `You answer questions about one PDF, which the first user message contains page by page as "[page N] text".
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

async function documentContext() {
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
    if (!question || state.busy) {
        return;
    }
    state.busy = true;
    elements.question.value = "";
    elements.status.textContent = "读取文档…";
    appendMessage("user", escapeHtml(question).replaceAll("\n", "<br>"));

    try {
        if (!state.history.length) {
            const context = await documentContext();
            state.history.push({ role: "user", content: `${context}\n\nQuestion: ${question}` });
        } else {
            state.history.push({ role: "user", content: question });
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
        elements.status.textContent = "";
    } catch (error) {
        const message = String(error.message || error);
        appendMessage(
            "error",
            escapeHtml(message === "ai-not-configured" ? "请先在 Mengshee 的“通用 AI”设置里配置模型和 API 地址。" : message)
        );
        state.history.pop();
        elements.status.textContent = "";
    } finally {
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
        // Models do not always put the label in the quoted part; the visible
        // link text often carries it. Try each candidate in the reader, which
        // keeps the first one it can locate on the page.
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

elements.send.addEventListener("click", ask);
elements.question.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !event.shiftKey) {
        event.preventDefault();
        ask();
    }
});
elements.question.addEventListener("input", () => {
    elements.question.style.height = "auto";
    elements.question.style.height = `${Math.min(elements.question.scrollHeight, 160)}px`;
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
