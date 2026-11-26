/**
 * Opening and saving files on three platforms: the Tauri desktop app (native dialogs, file
 * watching), browsers with the File System Access API, and everything else (file input and
 * download).
 */

export interface FileHandle {
  readonly name: string;
  save(text: string): Promise<void>;
  /** Calls `onChange` with the new text when the file changes on disk; returns a stop function. */
  watch?(onChange: (text: string) => void): Promise<() => void>;
}

export interface OpenedFile {
  readonly name: string;
  readonly text: string;
  /** The raw bytes, for binary formats such as draw.io PNGs. */
  readonly bytes: Uint8Array;
  /** Present when the file can be saved back in place. */
  readonly handle?: FileHandle;
}

export const isTauri = (): boolean =>
  typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;

// The File System Access API is not in TypeScript's DOM types yet.
interface FsFileHandle {
  readonly name: string;
  getFile(): Promise<File>;
  createWritable(): Promise<{ write(data: string): Promise<void>; close(): Promise<void> }>;
}
interface FsWindow {
  showOpenFilePicker?(options: unknown): Promise<FsFileHandle[]>;
  showSaveFilePicker?(options: unknown): Promise<FsFileHandle>;
}
const fsWindow = (): FsWindow => window as unknown as FsWindow;

const baseName = (path: string): string => path.split(/[\\/]/).pop() ?? path;

function browserHandle(h: FsFileHandle): FileHandle {
  return {
    name: h.name,
    async save(text) {
      const w = await h.createWritable();
      await w.write(text);
      await w.close();
    },
  };
}

async function tauriHandle(path: string): Promise<FileHandle> {
  const fs = await import('@tauri-apps/plugin-fs');
  return {
    name: baseName(path),
    save: (text) => fs.writeTextFile(path, text),
    async watch(onChange) {
      const stop = await fs.watch(path, async () => onChange(await fs.readTextFile(path)), {
        delayMs: 200,
      });
      return stop;
    },
  };
}

/** Asks for a file; resolves to undefined when the user cancels. */
export async function openFile(extensions: readonly string[]): Promise<OpenedFile | undefined> {
  if (isTauri()) {
    const { open } = await import('@tauri-apps/plugin-dialog');
    const fs = await import('@tauri-apps/plugin-fs');
    const path = await open({
      multiple: false,
      filters: [{ name: 'Diagrams', extensions: extensions.map((e) => e.replace(/^\./, '')) }],
    });
    if (typeof path !== 'string') return undefined;
    const bytes = await fs.readFile(path);
    return {
      name: baseName(path),
      text: new TextDecoder().decode(bytes),
      bytes,
      handle: await tauriHandle(path),
    };
  }
  const picker = fsWindow().showOpenFilePicker;
  if (picker) {
    try {
      const [h] = await picker({
        types: [{ description: 'Diagrams', accept: { 'text/plain': [...extensions] } }],
      });
      if (!h) return undefined;
      const file = await h.getFile();
      const bytes = new Uint8Array(await file.arrayBuffer());
      return {
        name: h.name,
        text: new TextDecoder().decode(bytes),
        bytes,
        handle: browserHandle(h),
      };
    } catch {
      return undefined; // cancelled
    }
  }
  return new Promise((resolve) => {
    const input = Object.assign(document.createElement('input'), {
      type: 'file',
      accept: extensions.join(','),
    });
    input.onchange = async () => {
      const file = input.files?.[0];
      if (!file) return resolve(undefined);
      const bytes = new Uint8Array(await file.arrayBuffer());
      resolve({ name: file.name, text: new TextDecoder().decode(bytes), bytes });
    };
    input.click();
  });
}

/** Asks where to save; resolves to a handle for later saves, or undefined if cancelled. */
export async function saveFileAs(
  text: string,
  suggestedName: string,
): Promise<FileHandle | undefined> {
  if (isTauri()) {
    const { save } = await import('@tauri-apps/plugin-dialog');
    const path = await save({
      defaultPath: suggestedName,
      filters: [{ name: 'DiaDraw', extensions: ['draw'] }],
    });
    if (!path) return undefined;
    const handle = await tauriHandle(path);
    await handle.save(text);
    return handle;
  }
  const picker = fsWindow().showSaveFilePicker;
  if (picker) {
    try {
      const handle = browserHandle(
        await picker({
          suggestedName,
          types: [{ description: 'DiaDraw', accept: { 'text/plain': ['.draw'] } }],
        }),
      );
      await handle.save(text);
      return handle;
    } catch {
      return undefined;
    }
  }
  download(new Blob([text], { type: 'text/plain' }), suggestedName);
  return undefined;
}

export function download(blob: Blob, name: string): void {
  const url = URL.createObjectURL(blob);
  const a = Object.assign(document.createElement('a'), { href: url, download: name });
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/** Rasterises an SVG string in the browser. */
export async function svgToPng(svg: string, scale = 2): Promise<Blob> {
  const width = Number(/<svg[^>]*\swidth="([\d.]+)"/.exec(svg)?.[1] ?? 800);
  const height = Number(/<svg[^>]*\sheight="([\d.]+)"/.exec(svg)?.[1] ?? 600);
  const url = URL.createObjectURL(new Blob([svg], { type: 'image/svg+xml' }));
  try {
    const img = new Image();
    img.src = url;
    await img.decode();
    const canvas = Object.assign(document.createElement('canvas'), {
      width: width * scale,
      height: height * scale,
    });
    const ctx = canvas.getContext('2d')!;
    ctx.scale(scale, scale);
    ctx.drawImage(img, 0, 0, width, height);
    return await new Promise<Blob>((resolve, reject) =>
      canvas.toBlob((b) => (b ? resolve(b) : reject(new Error('PNG export failed'))), 'image/png'),
    );
  } finally {
    URL.revokeObjectURL(url);
  }
}
