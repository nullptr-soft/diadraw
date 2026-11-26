/**
 * draw.io's stencil sets (converted to `drawio-*` libraries) are separate assets, fetched only
 * when a diagram, an import or the palette needs one, and handed to the core.
 */
import type { DiaDraw } from '@diadraw/wasm';

const files = import.meta.glob('../../../../assets/drawio/**/*.json.gz', {
  query: '?url',
  import: 'default',
  // Only the URLs are bundled (eagerly); the files themselves are fetched on demand.
  eager: true,
}) as Record<string, string>;

export interface DrawioSetInfo {
  readonly name: string;
  readonly version: string;
  readonly source: string;
  readonly shapes: number;
  readonly vendor: boolean;
}

export interface DrawioIndex {
  readonly drawio: { readonly version: string };
  readonly licenseNotes: string;
  readonly sets: readonly DrawioSetInfo[];
}

async function fetchJsonText(file: string): Promise<string> {
  const url = files[`../../../../assets/drawio/${file}`];
  if (!url) throw new Error(`No draw.io stencil file ${file}`);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${file}: HTTP ${res.status}`);
  const bytes = new Uint8Array(await res.arrayBuffer());
  // A server may already have undone the gzip (Content-Encoding); then it is plain JSON.
  if (bytes[0] !== 0x1f || bytes[1] !== 0x8b) return new TextDecoder().decode(bytes);
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
  return new Response(stream).text();
}

let index: Promise<DrawioIndex | undefined> | undefined;
const sets = new Map<string, Promise<boolean>>();

/** The index of sets; also given to the core so draw.io imports draw stencils with artwork. */
export function drawioIndex(dd: DiaDraw): Promise<DrawioIndex | undefined> {
  index ??= fetchJsonText('index.json.gz')
    .then((text) => {
      dd.loadDrawioIndex(text);
      return JSON.parse(text) as DrawioIndex;
    })
    .catch(() => {
      index = undefined;
      return undefined;
    });
  return index;
}

export const isDrawioLibrary = (name: string): boolean => name.startsWith('drawio-');

/** Loads a set into the core (once); resolves to whether it is available. */
export function loadDrawioSet(dd: DiaDraw, name: string): Promise<boolean> {
  let p = sets.get(name);
  if (!p) {
    p = fetchJsonText(`sets/${name}.json.gz`)
      .then((text) => {
        dd.addLibrary(text);
        return true;
      })
      .catch(() => {
        sets.delete(name);
        return false;
      });
    sets.set(name, p);
  }
  return p;
}

/** Whether a set was loaded (or is loading). */
export const drawioSetRequested = (name: string): boolean => sets.has(name);

/** The alias a draw.io set is used under, as the draw.io importer names it. */
export const drawioAlias = (name: string): string =>
  name.replace(/^drawio-/, '').replace(/[^A-Za-z0-9_]+/g, '_');
