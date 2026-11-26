/** Saves a drawn diagram as an SVG or PNG file from the browser. */

function save(blob: Blob, fileName: string): void {
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = fileName;
  document.body.append(link);
  link.click();
  link.remove();
  // The click starts the download synchronously; the URL can go once it has.
  setTimeout(() => URL.revokeObjectURL(url), 0);
}

/** A file name for a view: letters, digits, dashes and dots only. */
export function fileNameFor(view: string, extension: 'svg' | 'png'): string {
  const base = view.replace(/[^\w.-]+/g, '-').replace(/^-+|-+$/g, '') || 'diagram';
  return `${base}.${extension}`;
}

/** The SVG as a standalone file: with the namespace and XML declaration a viewer expects. */
export function standaloneSvg(svg: string): string {
  let out = svg.replace(/^<svg style="[^"]*"/, '<svg');
  if (!/^<svg[^>]*\sxmlns=/.test(out))
    out = out.replace(/^<svg/, '<svg xmlns="http://www.w3.org/2000/svg"');
  return `<?xml version="1.0" encoding="UTF-8"?>\n${out}`;
}

export function downloadSvg(svg: string, fileName: string): void {
  save(new Blob([standaloneSvg(svg)], { type: 'image/svg+xml' }), fileName);
}

/** Draws the SVG on a canvas at `scale` times its size and saves that as PNG. */
export async function downloadPng(svg: string, fileName: string, scale = 2): Promise<void> {
  const image = new Image();
  image.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(standaloneSvg(svg))}`;
  await image.decode();
  const canvas = document.createElement('canvas');
  canvas.width = Math.ceil(image.naturalWidth * scale);
  canvas.height = Math.ceil(image.naturalHeight * scale);
  const context = canvas.getContext('2d');
  if (!context) throw new Error('this browser cannot draw on a canvas');
  context.drawImage(image, 0, 0, canvas.width, canvas.height);
  const blob = await new Promise<Blob | null>((resolve) => canvas.toBlob(resolve, 'image/png'));
  if (!blob) throw new Error('could not encode the PNG');
  save(blob, fileName);
}
