// Serve only experiment assets, not the repository/private tree.
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
const root = new URL('../../', import.meta.url);
const paths = new Map([
  ['/experiments/wasm/browser.html', 'text/html; charset=utf-8'],
  ['/experiments/wasm/runtime.mjs', 'text/javascript'],
  ['/build-wasm/taiyin_probe.mjs', 'text/javascript'],
  ['/build-wasm/taiyin_probe.wasm', 'application/wasm'],
]);
for (const body of ['sun', 'moon', 'emb', 'mercury', 'venus', 'mars',
                    'jupiter', 'saturn', 'uranus', 'neptune', 'pluto']) {
  paths.set(`/data/ephemerides/opm2/major-bodies/de442-full/${body}.opm2`,
            'application/octet-stream');
}
createServer(async (request, response) => {
  const path = new URL(request.url, 'http://127.0.0.1').pathname;
  if (request.method !== 'GET' || !paths.has(path)) {
    response.writeHead(404).end(); return;
  }
  try {
    const bytes = await readFile(new URL(path.slice(1), root));
    response.writeHead(200, {'Content-Type': paths.get(path),
      'Content-Length': bytes.length, 'Cache-Control': 'no-store'}).end(bytes);
  } catch { response.writeHead(404).end(); }
}).listen(8765, '127.0.0.1', () => {
  console.log('http://127.0.0.1:8765/experiments/wasm/browser.html');
});
