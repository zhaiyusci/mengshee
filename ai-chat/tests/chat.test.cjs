const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const assert = require('node:assert/strict');
const test = require('node:test');
const source = fs.readFileSync(path.join(__dirname, '../src/app.js'), 'utf8');
const completion = { choices: [{ message: { content: 'Test answer' } }] };
const tick = () => new Promise(resolve => setImmediate(resolve));

async function harness(handler = () => completion, mathError = false) {
    const nodes = new Map(), intervals = [], calls = [];
    let current = { pages: 1, page: 1, id: 'fixture', revision: '1', title: 'Test PDF' };
    function element() {
        return { value: '', checked: false, style: {}, dataset: {}, children: [], listeners: {}, scrollHeight: 44,
            addEventListener(k, f) { this.listeners[k] = f; }, append(x) { this.children.push(x); }, replaceChildren() { this.children = []; } };
    }
    const context = vm.createContext({
        window: { __TAURI__: { core: { invoke: async (_, { method, params }) => {
            calls.push({ method, params: JSON.parse(JSON.stringify(params)) });
            if (method === 'state') return { document: { ...current } };
            return handler(method, params);
        } } }, MathJax: { typesetPromise: async () => { if (mathError) throw Error('render failed'); } } },
        document: { getElementById(id) { if (!nodes.has(id)) nodes.set(id, element()); return nodes.get(id); }, createElement: element },
        localStorage: { getItem() { return '0'; }, setItem() {} }, setInterval(fn) { intervals.push(fn); }, Intl, console
    });
    vm.runInContext(source, context);
    await tick();
    return { nodes, calls, intervals, run: code => vm.runInContext(code, context), setDocument(value) { current = { ...current, ...value }; },
        asks() { return calls.filter(x => x.method === 'ask'); },
        text() { return JSON.stringify(nodes.get('chat').children); } };
}
const pageHandler = method => method === 'pageBlocks' ? { blocks: [{id: 'btestp1l1', text: 'PDF content'}] } : method === 'pageImage' ? { data: 'AA', mime: 'image/jpeg' } : completion;

test('citation sends IDs and captured revision, never the displayed text', async () => {
    const h = await harness(() => ({ located: true, page: 2, pages: 1 }));
    const link = { dataset: { blockIds: 'btestp2l1', revision: '1' }, textContent: 'Page 2' };
    await h.nodes.get('chat').listeners.click({ target: { closest: () => link } });
    assert.deepEqual(h.calls.find(x => x.method === 'jump').params, {blockIds: ['btestp2l1'], revision: '1'});
    assert.equal(h.asks().length, 0);
});

test('multi-line and multi-page citation preserves ID order without duplicates', async () => {
    const h = await harness(() => ({ located: true, page: 2, pages: 2 }));
    const link = { dataset: { blockIds: 'btestp2l1,btestp2l2,btestp3l1,btestp2l1', revision: '1' }, textContent: 'results' };
    await h.nodes.get('chat').listeners.click({ target: { closest: () => link } });
    assert.deepEqual(h.calls.find(x => x.method === 'jump').params.blockIds, ['btestp2l1', 'btestp2l2', 'btestp3l1']);
    assert.match(h.nodes.get('status').textContent, /共 2 页证据/);
});

test('invalid ID reports failure without searching text or calling the model', async () => {
    const h = await harness(() => { throw Error('citation-unavailable'); });
    const link = { dataset: { blockIds: 'invented', revision: '1' }, textContent: 'Page 2 contains results' };
    await h.nodes.get('chat').listeners.click({ target: { closest: () => link } });
    assert.deepEqual(h.calls.filter(x => x.method !== 'state').map(x => x.method), ['jump']);
    assert.match(h.nodes.get('status').textContent, /证据编号无效/);
});

test('page-only image citation navigates without promising a text highlight', async () => {
    const h = await harness(() => ({ located: false, page: 4 }));
    await h.nodes.get('chat').listeners.click({ target: { closest: () => ({dataset: {page:'4',revision:'1'}}) } });
    assert.deepEqual(h.calls.find(x => x.method === 'jump').params, {page:4,revision:'1'});
    assert.match(h.nodes.get('status').textContent, /没有文字高亮/);
    assert.equal(h.asks().length, 0);
});

test('renderer captures revision on both ID and page links and escapes link labels', async () => {
    const h = await harness(pageHandler);
    const rendered = h.run('renderMarkdown("[<evidence>](cite:btestp1l1,btestp1l2) [page](pdf:4)", "7")');
    assert.match(rendered, /data-ranges="btestp1l1,btestp1l2" data-revision="7"/);
    assert.match(rendered, /&lt;evidence&gt;/);
    assert.match(rendered, /data-page="4" data-revision="7"/);
    h.setDocument({revision:'8'});
    await h.intervals[0]();
    assert.match(rendered, /data-revision="7"/);
});

test('range citation sends endpoints unchanged, including column or page continuation', async () => {
    const h = await harness(() => ({located:true,page:4,pages:1,blocks:9}));
    const ranges = 'btestp4l58..btestp4l66,btestp1l9..btestp2l3';
    const rendered = h.run(`renderMarkdown("[evidence](cite:${ranges})", "7")`);
    assert.match(rendered, /data-ranges="btestp4l58\.\.btestp4l66,btestp1l9\.\.btestp2l3"/);
    await h.nodes.get('chat').listeners.click({target:{closest:()=>({dataset:{ranges,revision:'7'}})}});
    assert.deepEqual(h.calls.find(x=>x.method==='jump').params, {ranges:[
        {startId:'btestp4l58',endId:'btestp4l66'}, {startId:'btestp1l9',endId:'btestp2l3'}
    ],revision:'7'});
    assert.equal(h.asks().length,0);
});

test('single block uses a degenerate range and malformed ranges do not become links', async () => {
    const h = await harness(pageHandler);
    await h.nodes.get('chat').listeners.click({target:{closest:()=>({dataset:{ranges:'btestp1l1..btestp1l1'}})}});
    assert.deepEqual(h.calls.find(x=>x.method==='jump').params.ranges,[{startId:'btestp1l1',endId:'btestp1l1'}]);
    assert.doesNotMatch(h.run('renderMarkdown("[bad](cite:btestp1l1..) [bad](cite:..btestp1l2)")'), /<a /);
});

test('invalid range has a specific error and no model repair request', async () => {
    const h = await harness(()=>{throw Error('citation-range-invalid');});
    await h.nodes.get('chat').listeners.click({target:{closest:()=>({dataset:{ranges:'btestp1l9..btestp1l2'}})}});
    assert.match(h.nodes.get('status').textContent,/起止范围无效/);
    assert.equal(h.asks().length,0);
});

test('stale citation keeps its original revision and never adopts the current document', async () => {
    const h = await harness(() => { throw Error('document-changed'); });
    h.setDocument({revision:'2'});
    await h.nodes.get('chat').listeners.click({target:{closest:()=>({dataset:{blockIds:'btestp1l1',revision:'1'}})}});
    assert.equal(h.calls.find(x => x.method === 'jump').params.revision,'1');
    assert.match(h.nodes.get('status').textContent,/引用已失效/);
});

test('image mode includes the same indexed text used by ID citations', async () => {
    const h = await harness(pageHandler);
    h.run('state.useImages = true');
    await h.run('runAsk("evidence")');
    const sent = JSON.stringify(h.asks()[0]);
    assert.match(sent,/image_url/);
    assert.match(sent,/btestp1l1/);
    assert.match(sent,/PDF content/);
    assert.equal(h.calls.some(x => x.method === 'pageText'),false);
});

test('composition Enter is ignored; ordinary Enter sends exactly once', async () => {
    const h = await harness(pageHandler);
    const input = h.nodes.get('question');
    input.value = '正在选词';
    const key = overrides => ({ key: 'Enter', preventDefault() {}, ...overrides });
    input.listeners.keydown(key({ isComposing: true }));
    input.listeners.keydown(key({ keyCode: 229 }));
    input.listeners.keydown(key({ shiftKey: true }));
    await tick();
    assert.equal(h.asks().length, 0);
    assert.equal(input.value, '正在选词');
    input.listeners.keydown(key({}));
    await tick();
    assert.equal(h.asks().length, 1);
});

test('MathJax failure preserves the completed turn for the next question', async () => {
    const h = await harness(pageHandler, true);
    await h.run('runAsk("first")');
    await h.run('runAsk("second")');
    assert.deepEqual(Array.from(h.run('state.history.map(x => x.role)')), ['user', 'assistant', 'user', 'assistant']);
    assert.equal(h.asks()[1].params.messages.filter(x => x.role === 'assistant').length, 1);
    assert.match(h.text(), /公式排版失败/);
    assert.equal(h.run('state.busy'), false);
});

test('switching image mode on and off refreshes context while retaining dialogue', async () => {
    const h = await harness(pageHandler);
    await h.run('runAsk("first")');
    h.nodes.get('use-images').checked = true;
    h.nodes.get('use-images').listeners.change();
    await h.run('runAsk("second")');
    assert.match(JSON.stringify(h.asks()[1]), /image_url/);
    assert.equal(h.asks()[1].params.messages.filter(x => x.role === 'assistant').length, 1);
    h.nodes.get('use-images').checked = false;
    h.nodes.get('use-images').listeners.change();
    await h.run('runAsk("third")');
    assert.doesNotMatch(JSON.stringify(h.asks()[2]), /image_url/);
    assert.match(JSON.stringify(h.asks()[2]), /PDF content/);
});

test('image rejection on a follow-up rebuilds text context without old images', async () => {
    let asks = 0;
    const h = await harness(method => {
        if (method === 'ask' && ++asks === 2) throw Error('HTTP 400 image unsupported');
        return pageHandler(method);
    });
    h.run('state.useImages = true');
    await h.run('runAsk("first")');
    await h.run('runAsk("follow-up")');
    assert.equal(h.asks().length, 3);
    assert.match(JSON.stringify(h.asks()[1]), /image_url/);
    assert.doesNotMatch(JSON.stringify(h.asks()[2]), /image_url/);
    assert.match(JSON.stringify(h.asks()[2]), /PDF content/);
    assert.equal(h.run('state.history.length'), 4);
});

test('user attachments are never silently removed by a text fallback', async () => {
    const h = await harness(method => {
        if (method === 'ask') throw Error('HTTP 400 image unsupported');
        return pageHandler(method);
    });
    h.run('state.useImages = true');
    await h.run('runAsk("photo", [{url:"data:image/jpeg;base64,USER"}])');
    assert.equal(h.asks().length, 1);
    assert.match(JSON.stringify(h.asks()[0]), /USER/);
    assert.equal(h.run('state.history.length'), 0);
});

test('length finish reason keeps partial answer and shows a persistent warning', async () => {
    const h = await harness(method => method === 'ask' ? { choices: [{ finish_reason: 'length', message: { content: 'unfinished...' } }] } : pageHandler(method));
    await h.run('runAsk("first")');
    assert.match(h.text(), /输出长度限制/);
    assert.equal(h.run('state.history[1].content'), 'unfinished...');
});

test('same-path same-page-count revision changes refresh content and drop stale answers', async () => {
    let text = 'OLD PDF';
    const h = await harness(method => method === 'pageBlocks' ? { blocks: [{id: 'block', text}] } : completion);
    await h.run('runAsk("first")');
    text = 'NEW PDF';
    h.setDocument({ revision: '2' });
    // No polling: the pre-request refresh must catch the edit.
    await h.run('runAsk("second")');
    assert.doesNotMatch(JSON.stringify(h.asks()[1].params.messages.slice(1)), /OLD PDF|Test answer|first/);
    assert.match(JSON.stringify(h.asks()[1]), /NEW PDF/);
    assert.equal(h.asks()[1].params.revision, '2');
});

test('ordinary page navigation preserves text cache and conversation', async () => {
    const h = await harness(pageHandler);
    h.setDocument({ pages: 2 });
    await h.run('runAsk("first")');
    h.setDocument({ page: 2 });
    await h.intervals[0]();
    await h.run('runAsk("second")');
    assert.equal(h.calls.filter(x => x.method === 'pageBlocks').length, 2);
    assert.equal(h.run('state.history.length'), 4);
});

test('document change during a request clears stale context and reports retry', async () => {
    const h = await harness(method => {
        if (method === 'ask') throw Error('document-changed');
        return pageHandler(method);
    });
    await h.run('runAsk("first")');
    assert.equal(h.run('state.history.length'), 0);
    assert.equal(h.run('state.context'), null);
    assert.match(h.text(), /文档在问答过程中发生变化/);
});

test('request failure preserves earlier successful turns', async () => {
    let asks = 0;
    const h = await harness(method => {
        if (method === 'ask' && ++asks === 2) throw Error('connection lost');
        return pageHandler(method);
    });
    await h.run('runAsk("first")');
    await h.run('runAsk("failed")');
    await h.run('runAsk("retry")');
    assert.equal(h.run('state.history.length'), 4);
    assert.doesNotMatch(JSON.stringify(h.asks()[2]), /failed/);
    assert.match(JSON.stringify(h.asks()[2]), /Test answer/);
});

test('limited images include current page first and report actual page numbers', async () => {
    const h = await harness(pageHandler);
    h.setDocument({ pages: 30, page: 25 });
    h.run('state.useImages = true');
    await h.run('runAsk("current diagram")');
    const pages = h.calls.filter(x => x.method === 'pageImage').map(x => x.params.page);
    assert.equal(pages.length, 16);
    assert.equal(pages[0], 25);
    assert.match(h.text(), /16 张页面图片/);
    h.setDocument({ page: 1 });
    await h.run('runAsk("new page")');
    assert.equal(h.calls.filter(x => x.method === 'pageImage')[16].params.page, 1);
});

test('byte cap reports one sent image, not sixteen', async () => {
    const h = await harness(() => ({ data: 'A'.repeat(13 * 1024 * 1024), mime: 'image/jpeg' }));
    h.run('state.document = { pages: 16, page: 7 }');
    const content = await h.run('documentImages()');
    assert.equal(content.filter(x => x.type === 'image_url').length, 1);
    assert.match(content[0].text, /发送 1 张页面图片：第 7 页/);
    assert.match(h.text(), /发送 1 张页面图片/);
});
