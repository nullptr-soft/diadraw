/**
 * The language server process: the C++ server inside the WebAssembly module, behind the Language
 * Server Protocol's stdio framing. Everything else (diagnostics, outline, definition, hover,
 * completion, libraries on disk) is the core's.
 */
import { loadDiaDraw } from '@diadraw/wasm';
import { nodeFileSystem } from '@diadraw/wasm/node';
import { MessageReader } from './framing';

async function main(): Promise<void> {
  const dd = await loadDiaDraw();
  dd.setFileSystem(nodeFileSystem(), { drawio: process.env.DIADRAW_DRAWIO ?? '' });
  const server = dd.createLanguageServer({ cwd: process.cwd() });
  server.on('message', (message) => {
    const body = Buffer.from(JSON.stringify(message), 'utf8');
    process.stdout.write(`Content-Length: ${body.length}\r\n\r\n`);
    process.stdout.write(body);
  });
  const reader = new MessageReader((text) => {
    server.send(text);
    const code = server.exitCode;
    if (code !== undefined) process.exit(code);
  });
  process.stdin.on('data', (chunk: Buffer) => reader.push(chunk));
  process.stdin.on('end', () => process.exit(1));
}

void main();
