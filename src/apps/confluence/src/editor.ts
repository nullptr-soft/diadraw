/**
 * The DiaDraw macro's editor, which Confluence opens when the macro is inserted and from its Edit
 * button: the DiaDraw text beside a live preview, pickers for the view and the theme, and import
 * from Mermaid, PlantUML and the other formats the core reads. Save stores the text with the
 * macro (`view.submit`); Cancel leaves the macro as it was.
 */
import { view } from '@forge/bridge';
import { loadDiaDraw, type DiaDraw } from '@diadraw/wasm';
import {
  IMPORT_FORMATS,
  STARTER_SOURCE,
  draw,
  importFileName,
  importText,
  readConfig,
  withTheme,
  type ImportFormat,
  type MacroConfig,
} from './diagram.js';

function element<T extends HTMLElement>(id: string): T {
  return document.getElementById(id) as T;
}

const text = element<HTMLTextAreaElement>('source');
const preview = element<HTMLDivElement>('preview');
const problems = element<HTMLUListElement>('problems');
const viewPicker = element<HTMLSelectElement>('view');
const themePicker = element<HTMLSelectElement>('theme');
const saveButton = element<HTMLButtonElement>('save');
const cancelButton = element<HTMLButtonElement>('cancel');
const importButton = element<HTMLButtonElement>('import');
const importDialog = element<HTMLDialogElement>('import-dialog');
const importFormat = element<HTMLSelectElement>('import-format');
const importFile = element<HTMLInputElement>('import-file');
const importSource = element<HTMLTextAreaElement>('import-source');
const importNotes = element<HTMLParagraphElement>('import-notes');
const importApply = element<HTMLButtonElement>('import-apply');
const status = element<HTMLParagraphElement>('status');

/** The view the user picked; kept while the text changes, even if a typo hides it for a while. */
let chosenView: string | undefined;

function setOptions(select: HTMLSelectElement, values: readonly string[], selected: string): void {
  const current = [...select.options].map((o) => o.value);
  if (current.length !== values.length || current.some((v, i) => v !== values[i])) {
    select.replaceChildren(
      ...values.map((value) => {
        const option = document.createElement('option');
        option.value = option.textContent = value;
        return option;
      }),
    );
  }
  select.value = selected;
}

function config(): MacroConfig {
  return { source: text.value, ...(chosenView && { view: chosenView }) };
}

function redraw(dd: DiaDraw): void {
  const drawing = draw(dd, config(), 'dd-');
  setOptions(
    viewPicker,
    drawing.views,
    drawing.result.ok ? drawing.result.view : (chosenView ?? ''),
  );
  viewPicker.disabled = drawing.views.length < 2;
  setOptions(themePicker, drawing.themes, drawing.theme);
  if (drawing.result.ok) {
    preview.innerHTML = drawing.result.svg;
    preview.classList.remove('stale');
  } else {
    // Keeps the last good drawing, dimmed, while the text has errors.
    preview.classList.add('stale');
  }
  problems.replaceChildren(
    ...drawing.diagnostics.map((d) => {
      const li = document.createElement('li');
      li.className = d.severity;
      li.textContent = `${d.line}:${d.column}: ${d.message}`;
      li.addEventListener('click', () => {
        text.focus();
        text.setSelectionRange(d.from, d.to);
      });
      return li;
    }),
  );
  if (!drawing.result.ok && !drawing.diagnostics.length) {
    const li = document.createElement('li');
    li.className = 'error';
    li.textContent = drawing.result.message;
    problems.append(li);
  }
}

/** Replaces the whole text as one undoable edit, so Ctrl+Z brings the old text back. */
function replaceText(source: string): void {
  text.focus();
  text.select();
  if (!document.execCommand('insertText', false, source)) text.value = source;
  text.setSelectionRange(0, 0);
  text.scrollTop = 0;
}

async function main(): Promise<void> {
  await view.theme.enable().catch(() => undefined);
  const context = await view.getContext();
  const saved = readConfig(context.extension?.config);
  text.value = saved.source || STARTER_SOURCE;
  chosenView = saved.view;

  const dd = await loadDiaDraw();
  let pending: ReturnType<typeof setTimeout> | undefined;
  const schedule = () => {
    clearTimeout(pending);
    pending = setTimeout(() => redraw(dd), 120);
  };
  redraw(dd);
  document.body.classList.remove('loading');
  text.focus();

  text.addEventListener('input', schedule);
  text.addEventListener('keydown', (event) => {
    if (
      event.key === 'Tab' &&
      !event.shiftKey &&
      !event.altKey &&
      !event.ctrlKey &&
      !event.metaKey
    ) {
      event.preventDefault();
      document.execCommand('insertText', false, '  ');
    } else if (event.key === 's' && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      saveButton.click();
    }
  });
  viewPicker.addEventListener('change', () => {
    chosenView = viewPicker.value;
    redraw(dd);
  });
  themePicker.addEventListener('change', () => {
    try {
      replaceText(withTheme(dd, text.value, chosenView ?? viewPicker.value, themePicker.value));
    } catch (error) {
      status.textContent = `Could not set the theme: ${String(error)}`;
    }
    redraw(dd);
  });

  saveButton.addEventListener('click', () => {
    saveButton.disabled = true;
    status.textContent = 'Saving…';
    view.submit({ config: config() }).catch((error: unknown) => {
      saveButton.disabled = false;
      status.textContent = `Could not save: ${String(error)}`;
    });
  });
  cancelButton.addEventListener('click', () => {
    void view.close();
  });

  for (const format of IMPORT_FORMATS) {
    const option = document.createElement('option');
    option.value = format.id;
    option.textContent = format.label;
    importFormat.append(option);
  }
  importButton.addEventListener('click', () => {
    importNotes.textContent = '';
    importDialog.showModal();
    importSource.focus();
  });
  importFile.addEventListener('change', async () => {
    const file = importFile.files?.[0];
    if (!file) return;
    importSource.value = await file.text();
    const guess = IMPORT_FORMATS.find((f) =>
      file.name.toLowerCase().endsWith(f.fileName.slice(f.fileName.lastIndexOf('.'))),
    );
    if (guess) importFormat.value = guess.id;
    else if (/\.(plantuml|pu|iuml)$/i.test(file.name)) importFormat.value = 'plantuml';
    else if (/\.(mermaid)$/i.test(file.name)) importFormat.value = 'mermaid';
    else if (/\.(xml)$/i.test(file.name)) importFormat.value = 'drawio';
    importFile.value = '';
  });
  importApply.addEventListener('click', (event) => {
    event.preventDefault();
    const outcome = importText(
      dd,
      importSource.value,
      importFileName(importFormat.value as ImportFormat),
    );
    if (!outcome.ok) {
      importNotes.textContent = `Could not import: ${outcome.message}`;
      return;
    }
    importDialog.close();
    chosenView = undefined;
    replaceText(outcome.source);
    redraw(dd);
    status.textContent = outcome.notes.length
      ? `Imported. Not carried over: ${outcome.notes.join('; ')}`
      : 'Imported.';
  });
}

main().catch((error: unknown) => {
  console.error('DiaDraw:', error);
  status.textContent = `DiaDraw could not start: ${error instanceof Error ? error.message : String(error)}`;
});
