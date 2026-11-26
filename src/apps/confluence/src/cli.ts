/**
 * `diadraw-confluence publish`: draws `.draw` files and attaches each view as SVG to a Confluence
 * Cloud page. The site and account come from options or from CONFLUENCE_BASE_URL,
 * CONFLUENCE_EMAIL and CONFLUENCE_API_TOKEN.
 */
import { parseArgs } from 'node:util';
import { loadDiaDraw } from '@diadraw/wasm';
import { nodeFileSystem } from '@diadraw/wasm/node';
import { Confluence, PublishError, renderFile, type Rendered } from './publish.js';

const usage = `Usage: diadraw-confluence publish <file.draw>... --page <id> [options]

Draws each view of the files as SVG and attaches it to the Confluence page, as a new version of
the attachment when it is there already. A diagram that has not changed is not uploaded again.

Options:
  --page <id>        The page to attach to (the number in the page's URL).
  --view <id>        Only this view, from each file that has it; repeat for several.
                     Every view by default.
  --insert           Also add an image of each new attachment to the end of the page.
  --dry-run          Draw the files and list the attachments, without contacting Confluence.
  --base-url <url>   https://<site>.atlassian.net (or CONFLUENCE_BASE_URL).
  --email <email>    The Atlassian account (or CONFLUENCE_EMAIL).
  The API token is read from CONFLUENCE_API_TOKEN only, so it stays out of shell history.
`;

export async function main(argv: readonly string[], env = process.env): Promise<number> {
  const { values, positionals } = parseArgs({
    args: [...argv],
    allowPositionals: true,
    options: {
      page: { type: 'string' },
      view: { type: 'string', multiple: true },
      insert: { type: 'boolean', default: false },
      'dry-run': { type: 'boolean', default: false },
      'base-url': { type: 'string' },
      email: { type: 'string' },
      help: { type: 'boolean', short: 'h', default: false },
    },
  });
  const [command, ...files] = positionals;
  if (values.help || command !== 'publish' || !files.length) {
    (values.help ? console.log : console.error)(usage);
    return values.help ? 0 : 2;
  }
  const dryRun = values['dry-run'];
  const baseUrl = values['base-url'] ?? env.CONFLUENCE_BASE_URL;
  const email = values.email ?? env.CONFLUENCE_EMAIL;
  const token = env.CONFLUENCE_API_TOKEN;
  const missing = [
    !values.page && '--page',
    !dryRun && !baseUrl && '--base-url or CONFLUENCE_BASE_URL',
    !dryRun && !email && '--email or CONFLUENCE_EMAIL',
    !dryRun && !token && 'CONFLUENCE_API_TOKEN',
  ].filter(Boolean);
  if (missing.length) {
    console.error(`diadraw-confluence: missing ${missing.join(', ')}\n\n${usage}`);
    return 2;
  }

  const dd = await loadDiaDraw();
  // Resolves `use` of libraries like the native CLI: diadraw-lock.json beside each file.
  dd.setFileSystem(nodeFileSystem(env));
  const rendered: Rendered[] = [];
  for (const file of files) rendered.push(...(await renderFile(dd, file, values.view)));
  const unknown = (values.view ?? []).filter((v) => !rendered.some((r) => r.view === v));
  if (unknown.length) {
    console.error(
      `diadraw-confluence: no file has the view ${unknown.map((v) => `"${v}"`).join(', ')}`,
    );
    return 1;
  }
  // Attachments are named after the file, so two files of one name would overwrite each other.
  const seen = new Map<string, Rendered>();
  for (const r of rendered) {
    const earlier = seen.get(r.fileName);
    if (earlier) {
      console.error(
        `diadraw-confluence: ${earlier.file} and ${r.file} would both be attached as ${r.fileName}; publish them to different pages`,
      );
      return 1;
    }
    seen.set(r.fileName, r);
  }

  if (dryRun) {
    for (const r of rendered) console.log(`${r.fileName}  (${r.file}, view ${r.view})`);
    return 0;
  }
  const confluence = new Confluence({ baseUrl: baseUrl!, email: email!, token: token! });
  for (const r of rendered) {
    const outcome = await confluence.publish(values.page!, r);
    console.log(`${outcome.padEnd(9)} ${r.fileName}  (${r.file}, view ${r.view})`);
  }
  if (values.insert) {
    const added = await confluence.insertImages(
      values.page!,
      rendered.map((r) => r.fileName),
    );
    if (added.length) console.log(`added to the page: ${added.join(', ')}`);
  }
  return 0;
}

main(process.argv.slice(2)).then(
  (code) => {
    process.exitCode = code;
  },
  (error: unknown) => {
    console.error(
      error instanceof PublishError ? error.message : `diadraw-confluence: ${String(error)}`,
    );
    process.exitCode = 1;
  },
);
