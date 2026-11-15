// A small chat window over the running Mengshee: it asks the model about the
// open document, renders Markdown + MathJax, and turns `[words](pdf:12)` links
// into jumps. Document access goes through the loopback bridge; the bridge
// token never leaves this machine.

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
    bridge: null,
    document: null,
    pageTexts: new Map(),
    history: [],
    busy: false
};

const systemPrompt = `You answer questions about one PDF, which the first user message contains page by page as "[page N] text".
Use only that document; never use outside knowledge. If it does not contain the answer, say so plainly.
Answer in the language of the question. Be concrete and direct; do not explain your reasoning.

Format: Markdown prose. Every factual claim links inline to the place in the document that supports it:
- [words](pdf:12) links to page 12 (the page's position in the file, 1 for the first page).
- [see (14)](pdf:12 "(14)") also names the printed object there: a label such as (14), Figure 5, Section 3.1, or a short verbatim phrase.
Write formulas as ASCII LaTeX: inline $...$, display $$...$$. Never paste Unicode math letters.
Follow-up questions continue the same conversation.`;

async function bridge(path, options = {}) {
    if (!state.bridge) {
        throw new Error("没有找到正在运行的 Mengshee。");
    }
    const url = new URL(path, `http://127.0.0.1:${state.bridge.port}`);
    url.searchParams.set("token", state.bridge.token);
    const response = await window.fetch(url, options);
    return response;
}

async function connect() {
    if (!invoke) {
        elements.title.textContent = "需要在 Tauri 窗口中运行";
        return;
    }
    try {
        state.bridge = await invoke("bridge_info");
    } catch (error) {
        elements.title.textContent = "没有找到正在运行的 Mengshee";
        elements.status.textContent = String(error);
        return;
    }
    const documentState = await (await bridge("/state")).json();
    state.document = documentState.document?.pages ? documentState.document : null;
    renderDocumentState();
}

function renderDocumentState() {
    if (!state.document) {
        elements.title.textContent = "没有打开的文档";
        elements.meta.textContent = "在 Mengshee 里打开一份 PDF";
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
        const text = (await (await bridge(`/page/${page}/text`)).json()).text || "";
        state.pageTexts.set(page, text);
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
        const label = match[3] ? escapeHtml(match[3]) : "";
        html += `<a data-page="${page}" data-label="${label}">${escapeHtml(match[1])}</a>`;
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
        // The reader owns the model settings and the API key; this window only
        // sends the conversation.
        const response = await bridge("/ask", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                messages: [{ role: "system", content: systemPrompt }, ...state.history],
                temperature: 0.2
            })
        });
        const payload = await response.json();
        if (!response.ok) {
            throw new Error(
                payload?.error === "ai-not-configured"
                    ? "请先在 Mengshee 的“通用 AI”设置里配置模型和 API 地址。"
                    : payload?.error || `请求失败：${response.status}`
            );
        }
        const answer = payload?.choices?.[0]?.message?.content?.trim();
        if (!answer) {
            throw new Error(payload?.error?.message || "模型没有返回内容");
        }
        state.history.push({ role: "assistant", content: answer });
        const node = appendMessage("assistant", renderMarkdown(answer));
        await typeset(node);
        elements.status.textContent = "";
    } catch (error) {
        appendMessage("error", escapeHtml(String(error.message || error)));
        state.history.pop();
        elements.status.textContent = "";
    } finally {
        state.busy = false;
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
        const body = { page: Number(link.dataset.page) };
        if (link.dataset.label) {
            body.anchor = link.dataset.label;
        }
        const response = await bridge("/jump", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(body)
        });
        const result = await response.json();
        elements.status.textContent = result.located
            ? `已跳到第 ${link.dataset.page} 页并高亮`
            : `已跳到第 ${link.dataset.page} 页`;
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

// The window belongs to one document: when the reader closes it, or the
// reader itself goes away, the window closes too.
let missingDocumentCount = 0;
function closeWindow() {
    const current = window.__TAURI__?.window?.getCurrentWindow?.();
    if (current) {
        current.close();
    } else {
        window.close();
    }
}

setInterval(async () => {
    if (state.busy) {
        return;
    }
    try {
        const documentState = await (await bridge("/state")).json();
        const next = documentState.document?.pages ? documentState.document : null;
        missingDocumentCount = next ? 0 : missingDocumentCount + 1;
        if (missingDocumentCount >= 3) {
            closeWindow();
            return;
        }
        if (JSON.stringify(next) !== JSON.stringify(state.document)) {
            state.document = next;
            state.pageTexts.clear();
            renderDocumentState();
        }
    } catch {
        state.bridge = null;
        missingDocumentCount += 1;
        if (missingDocumentCount >= 3) {
            closeWindow();
            return;
        }
        elements.title.textContent = "正在等待 Mengshee…";
        await connect();
    }
}, 1500);
