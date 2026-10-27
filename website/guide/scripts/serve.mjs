import { createServer } from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import { resolve, extname, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../../.site/', import.meta.url));
const types = { '.html': 'text/html; charset=utf-8', '.css': 'text/css', '.js': 'text/javascript', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon', '.wasm': 'application/wasm' };
createServer(async (req, res) => {
  try {
    const pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
    if (pathname === '/mengshee') { res.writeHead(302, { Location: '/mengshee/' }); return res.end(); }
    if (!pathname.startsWith('/mengshee/')) { res.writeHead(404); return res.end(); }
    let file = resolve(root, pathname.slice('/mengshee/'.length));
    if (file !== resolve(root) && !file.startsWith(resolve(root) + sep)) { res.writeHead(403); return res.end(); }
    if ((await stat(file)).isDirectory()) {
      if (!pathname.endsWith('/')) { res.writeHead(302, { Location: pathname + '/' }); return res.end(); }
      file = resolve(file, 'index.html');
    }
    res.writeHead(200, { 'Content-Type': types[extname(file)] || 'application/octet-stream' });
    res.end(await readFile(file));
  } catch { res.writeHead(404); res.end('Not found'); }
}).listen(4321, '127.0.0.1', () => console.log('Preview: http://localhost:4321/mengshee/guide/'));
