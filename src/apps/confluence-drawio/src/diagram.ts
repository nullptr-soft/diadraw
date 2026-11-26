/**
 * The diagram as the macro stores it, shared by the macro (macro.ts) and its editor (editor.ts).
 *
 * The macro keeps one thing: the diagram as an SVG with the draw.io file embedded in it (what
 * draw.io calls an editable SVG, `xmlsvg`: the drawing plus a `content` attribute with the whole
 * `<mxfile>`, every page). The page shows the drawing without loading the editor, and the editor
 * opens the same SVG again. The SVG is compressed (raw deflate) and Base64-encoded, because the
 * macro's configuration is stored in the page and a drawing's SVG repeats a lot.
 */

/** The macro's configuration, as Confluence stores it with the macro. */
export interface MacroConfig {
  /** The editable SVG, deflated and Base64-encoded. Absent while the diagram is empty. */
  readonly diagram?: string;
}

/** Reads the stored configuration, ignoring anything that is not a string. */
export function readConfig(raw: unknown): MacroConfig {
  const diagram = (raw as Record<string, unknown> | null | undefined)?.diagram;
  return typeof diagram === 'string' && diagram ? { diagram } : {};
}

async function pipe(
  bytes: Uint8Array<ArrayBuffer>,
  stream: GenericTransformStream,
): Promise<Uint8Array> {
  const out = new Blob([bytes]).stream().pipeThrough(stream);
  return new Uint8Array(await new Response(out).arrayBuffer());
}

function toBase64(bytes: Uint8Array): string {
  let binary = '';
  // In chunks: String.fromCharCode takes its arguments on the stack.
  for (let i = 0; i < bytes.length; i += 0x8000)
    binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(binary);
}

function fromBase64(text: string): Uint8Array<ArrayBuffer> {
  const binary = atob(text);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

/** The SVG as the macro stores it. */
export async function packSvg(svg: string): Promise<string> {
  return toBase64(await pipe(new TextEncoder().encode(svg), new CompressionStream('deflate-raw')));
}

/** The SVG from the stored form; throws when it is not one `packSvg` wrote. */
export async function unpackSvg(packed: string): Promise<string> {
  const bytes = await pipe(fromBase64(packed), new DecompressionStream('deflate-raw'));
  const svg = new TextDecoder().decode(bytes);
  if (!/^\s*(<\?xml[^>]*>\s*)?<svg[\s>]/.test(svg))
    throw new Error('the stored diagram is not an SVG');
  return svg;
}

/** The SVG in a `data:` URI, as draw.io's `export` event sends it: Base64 or percent-encoded. */
export function svgFromDataUri(uri: string): string {
  const match = /^data:image\/svg\+xml(;[^,]*)?,(.*)$/s.exec(uri);
  if (!match) throw new Error('draw.io did not send an SVG');
  const [, params = '', body = ''] = match;
  if (/;base64/i.test(params)) return new TextDecoder().decode(fromBase64(body));
  return decodeURIComponent(body);
}

/** The opening `<svg …>` tag. */
function rootTag(svg: string): string {
  return /<svg[^>]*>/.exec(svg)?.[0] ?? '';
}

function attribute(tag: string, name: string): string | undefined {
  return new RegExp(`\\s${name}="([^"]*)"`).exec(tag)?.[1];
}

/**
 * Whether the SVG is an empty diagram: draw.io draws an empty page as a 1×1 SVG. A file with
 * several pages is never empty, since the empty one may just be the page that was open.
 */
export function isEmptyDiagram(svg: string): boolean {
  const tag = rootTag(svg);
  const pages = (attribute(tag, 'content') ?? '').match(/&lt;diagram[\s&]/g)?.length ?? 0;
  return pages <= 1 && attribute(tag, 'width') === '1px' && attribute(tag, 'height') === '1px';
}

/** The names of the diagram's pages, from the draw.io file the SVG carries. */
export function pageNames(svg: string): string[] {
  const content = attribute(rootTag(svg), 'content') ?? '';
  const names: string[] = [];
  for (const match of content.matchAll(/&lt;diagram\b(?:(?!&gt;).)*?\sname=&quot;(.*?)&quot;/g))
    names.push(decodeEntities(decodeEntities(match[1] ?? '')));
  return names;
}

function decodeEntities(text: string): string {
  return text.replace(/&(lt|gt|quot|apos|amp|#\d+|#x[\da-f]+);/gi, (entity, name: string) => {
    const named: Record<string, string> = { lt: '<', gt: '>', quot: '"', apos: "'", amp: '&' };
    if (name[0] !== '#') return named[name.toLowerCase()] ?? entity;
    const code = name[1] === 'x' || name[1] === 'X' ? parseInt(name.slice(2), 16) : +name.slice(1);
    return String.fromCodePoint(code);
  });
}

/**
 * The SVG for the page's light or dark theme. draw.io draws default colours with CSS
 * `light-dark()` under `color-scheme: light dark`, which an image would resolve by the reader's
 * system setting; this pins the scheme to the Confluence theme instead.
 */
export function withColorScheme(svg: string, scheme: 'light' | 'dark'): string {
  const tag = rootTag(svg);
  if (!tag) return svg;
  let next = tag.replace(/color-scheme:\s*[^;"]*/, `color-scheme: ${scheme}`);
  if (next === tag) {
    next = /\sstyle="/.test(tag)
      ? tag.replace(/\sstyle="/, ` style="color-scheme: ${scheme}; `)
      : tag.replace(/^<svg/, `<svg style="color-scheme: ${scheme}"`);
  }
  return svg.replace(tag, next);
}

/** The SVG as a `data:` URI for an image. */
export function svgDataUri(svg: string): string {
  return `data:image/svg+xml;base64,${toBase64(new TextEncoder().encode(svg))}`;
}

/** A file name from the first page's name: letters, digits, dashes and dots only. */
export function fileBaseName(svg: string): string {
  const name = pageNames(svg)[0] ?? '';
  return name.replace(/[^\w.-]+/g, '-').replace(/^-+|-+$/g, '') || 'diagram';
}
