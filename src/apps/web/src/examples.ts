/** The benchmark corpus doubles as the playground's examples. */
const files = import.meta.glob<string>('../../../../corpus/*.draw', {
  query: '?raw',
  import: 'default',
  eager: true,
});

export const examples: { name: string; source: string }[] = Object.entries(files)
  .map(([path, source]) => ({
    name: path
      .split('/')
      .pop()!
      .replace(/\.draw$/, ''),
    source,
  }))
  .sort((a, b) => a.name.localeCompare(b.name));
