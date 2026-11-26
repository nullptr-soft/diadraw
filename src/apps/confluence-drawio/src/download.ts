/** Saves the diagram as an SVG or PNG file from the browser. */

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

/** The SVG as a standalone file, with the XML declaration a viewer expects. */
export function standaloneSvg(svg: string): string {
  return svg.startsWith('<?xml') ? svg : `<?xml version="1.0" encoding="UTF-8"?>\n${svg}`;
}

export function downloadSvg(svg: string, fileName: string): void {
  save(new Blob([standaloneSvg(svg)], { type: 'image/svg+xml' }), fileName);
}

/** Draws the SVG on a white canvas at `scale` times its size and saves that as PNG. */
export async function downloadPng(svg: string, fileName: string, scale = 2): Promise<void> {
  const image = new Image();
  image.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}`;
  await image.decode();
  const canvas = document.createElement('canvas');
  canvas.width = Math.ceil(image.naturalWidth * scale);
  canvas.height = Math.ceil(image.naturalHeight * scale);
  const context = canvas.getContext('2d');
  if (!context) throw new Error('this browser cannot draw on a canvas');
  context.fillStyle = '#fff';
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.drawImage(image, 0, 0, canvas.width, canvas.height);
  const blob = await new Promise<Blob | null>((resolve) => canvas.toBlob(resolve, 'image/png'));
  if (!blob) throw new Error('could not encode the PNG');
  save(blob, fileName);
}
