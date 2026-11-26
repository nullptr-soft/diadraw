import type {
  Box,
  DiaDraw,
  Diagnostic,
  ExportPage,
  ImportReport,
  Element as ModelElement,
  Relationship as ModelRelationship,
  Model,
  Operation,
  PaletteItem,
  Point,
  RenderedView,
  Selection,
  Session,
  SessionChange,
  SessionState,
} from '@diadraw/wasm';
import { DiaDrawError } from '@diadraw/wasm';
import { localId } from './element-fields';
import type { AboutDialog } from './about';
import './about';
import type { DiagramCanvas } from './canvas';
import './canvas';
import { clearAction, EMPTY_DIAGRAM } from './clear-canvas';
import { examples } from './examples';
import type { FileHandle } from './files';
import { download, isTauri, openFile, saveFileAs, svgToPng } from './files';
import type { FeedbackKind } from './feedback';
import { describeSystem, feedbackUrl } from './feedback';
import { drawioIndex, drawioSetRequested, isDrawioLibrary, loadDrawioSet } from './drawio';
import type { FieldChoices } from './field-editors';
import { fieldChoices, selectorChoices } from './field-editors';
import type { ElementInspector } from './inspector';
import './inspector';
import type { StylePanel } from './style-panel';
import './style-panel';
import type { DocumentFields } from './style-fields';
import {
  documentFields,
  documentOperation,
  sharedStyles,
  sharedStylesOperation,
} from './style-fields';
import type { Draft } from './library';
import { DiagramLibrary } from './library';
import { browserClouds, SignInRequired } from './cloud';
import type { LocationId, StorageLocation } from './storage';
import { browserLocation, cloudLocation, editorFileName, plainName } from './storage';
import type { StoragePanel } from './storage-panel';
import './storage-panel';
import type { InsertDetail, ShapePalette } from './palette';
import './palette';
import { decodeSource, encodeSource } from './share';
import { attachSplitter } from './splitter';
import { GRID_SIZES, readGrid, shownStep, writeGrid } from './grid-settings';
import type { TextPane } from './text-pane';
import './text-pane';

const KINDS = [
  'process',
  'decision',
  'start',
  'end',
  'person',
  'system',
  'container',
  'component',
  'database',
  'note',
];
const FALLBACK = examples.find((e) => e.name.startsWith('c4-01'))?.source ?? 'model {\n}\n';

const TEMPLATE = `
  <header>
    <h1>DiaDraw<small class="app-version" title="DiaDraw version"></small><span class="file-name"></span></h1>
    <button type="button" data-action="open" title="Open (⌘O)">Open</button>
    <button type="button" data-action="save" title="Save (⌘S)">Save</button>
    <button type="button" data-action="storage" title="Open or save diagrams in this browser, Google Drive or OneDrive">Storage…</button>
    <button type="button" data-action="import" title="Import Mermaid, PlantUML, Structurizr, draw.io or BPMN">Import</button>
    <select aria-label="Example" data-role="examples"><option value="">Examples…</option></select>
    <select aria-label="View" data-role="views"></select>
    <select aria-label="View actions" data-role="view-actions" title="Add, rename or remove views">
      <option value="">⋯</option>
      <option value="new">New view…</option>
      <option value="rename">Rename view…</option>
      <option value="remove">Remove view</option>
    </select>
    <span class="divider"></span>
    <select aria-label="Add an element" data-role="add" title="Add an element of the chosen kind"><option value="">Add…</option></select>
    <button type="button" data-action="shapes" aria-pressed="false" title="Shapes and fragments from libraries">Shapes</button>
    <button type="button" data-action="grid" aria-pressed="false" title="Show the grid (⌘'); dragged elements line up with it">Grid</button>
    <select aria-label="Grid size" data-role="grid-size" title="Grid size (choosing one shows the grid)"></select>
    <button type="button" data-action="fit" title="Size the selected elements to fit their content (⌥⌘=)" disabled>Fit</button>
    <button type="button" data-action="undo" title="Undo (⌘Z)">Undo</button>
    <button type="button" data-action="redo" title="Redo (⇧⌘Z)">Redo</button>
    <button type="button" data-action="clear" title="Clear the canvas: start again from an empty diagram (Undo brings it back)">Clear</button>
    <span class="spacer"></span>
    <span class="status" role="status"></span>
    <button type="button" data-action="share">Share link</button>
    <button type="button" data-action="svg">SVG</button>
    <button type="button" data-action="png">PNG</button>
    <button type="button" data-action="drawio" title="Export all views as a draw.io file">draw.io</button>
    <button type="button" data-action="bpmn" title="Export all views as BPMN 2.0">BPMN</button>
    <span class="feedback-group">
      <button type="button" data-action="report-bug" title="Report a bug (opens a form on GitHub)">Report a bug</button>
      <button type="button" class="feedback" data-action="feedback" title="Send an idea, a wish or anything that could be easier (opens a form on GitHub)">Give feedback</button>
      <button type="button" data-action="about" title="Version, licence and the open-source software DiaDraw uses">About</button>
    </span>
  </header>
  <section class="panes">
    <dd-text></dd-text>
    <div class="splitter" role="separator" aria-orientation="vertical" aria-label="Resize the text pane" tabindex="0" data-splitter="text"></div>
    <div class="canvas-pane">
      <div class="report-slot"></div>
      <div class="messages"></div>
      <div class="canvas-row">
        <dd-palette hidden></dd-palette>
        <dd-canvas></dd-canvas>
        <div class="splitter" role="separator" aria-orientation="vertical" aria-label="Resize the properties panel" tabindex="0" data-splitter="properties"></div>
        <aside class="properties">
          <dd-inspector></dd-inspector>
          <dd-style-panel></dd-style-panel>
          <p class="properties-note" hidden></p>
        </aside>
      </div>
      <dd-storage></dd-storage>
      <dd-about></dd-about>
      <p class="help">Drag to move (Alt: place by the neighbours) · Shift- or ⌘-click, or drag over empty space, to select several · ⌘A selects all · drag the dot to connect · drag an end of a selected line to reconnect it ·
        double-click to rename · F2 renames the id everywhere · select one element or line to edit its properties · drag corners to resize (double-click one, or ⌥⌘=, to fit the content) and the round handle to turn · with Grid on, drags line up with it ·
        in fixed views, drag bend points to reroute · Delete to remove · ⌘Z / ⇧⌘Z undo and redo in both panes</p>
    </div>
  </section>
`;

/**
 * `<dd-app>`: the editor shell. It owns the core session; the text pane owns the text. Typing
 * updates the session; canvas gestures go to the session, whose text updates are applied to the
 * editor as transactions, so the text stays the source of truth and one undo history covers both.
 */
export class DiaDrawApp extends HTMLElement {
  dd: DiaDraw | undefined;
  #session: Session | undefined;
  /** The Id problem the inspector last asked about, for the model, element and Id it was for. */
  #idCheck: { model: Model; element: string; id: string; problem: string | undefined } | undefined;
  #model: Model | undefined;
  /** The libraries' palette items for the inspector, and the `use` lines they are for. */
  #libraryItems: readonly PaletteItem[] = [];
  #paletteFor = '';
  #diagnostics: readonly Diagnostic[] = [];
  #views: readonly string[] = [];
  /** The text the model, diagnostics and views above were read from. */
  #synced: string | undefined;
  #viewId: string | undefined;
  #rendered: RenderedView | undefined;
  #file: OpenDocument | undefined;
  #stopWatching: (() => void) | undefined;
  #timer: ReturnType<typeof setTimeout> | undefined;
  #statusTimer: ReturnType<typeof setTimeout> | undefined;
  #text!: TextPane;
  #canvas!: DiagramCanvas;
  #palette!: ShapePalette;
  #inspector!: ElementInspector;
  #stylePanel!: StylePanel;
  #report: (ImportReport & { file: string }) | undefined;
  /** Diagrams in the browser's IndexedDB; undefined where IndexedDB is unavailable. */
  #library: DiagramLibrary | undefined;
  /** This browser (when IndexedDB works), Google Drive and OneDrive. */
  #locations: StorageLocation[] = [];
  #storagePanel!: StoragePanel;
  #draftTimer: ReturnType<typeof setTimeout> | undefined;

  connectedCallback(): void {
    if (this.#text) return;
    this.classList.add('app');
    this.innerHTML = TEMPLATE;
    this.#text = this.querySelector('dd-text')!;
    this.#canvas = this.querySelector('dd-canvas')!;
    this.#palette = this.querySelector('dd-palette')!;
    this.#inspector = this.querySelector('dd-inspector')!;
    this.#stylePanel = this.querySelector('dd-style-panel')!;
    this.#storagePanel = this.querySelector('dd-storage')!;
    attachSplitter(
      this.querySelector('[data-splitter="text"]')!,
      this.#text,
      'before',
      this.#canvas,
    );
    attachSplitter(
      this.querySelector('[data-splitter="properties"]')!,
      this.querySelector('.properties')!,
      'after',
      this.#canvas,
    );
    this.#select<HTMLSelectElement>('add').append(
      ...KINDS.map((k) =>
        Object.assign(document.createElement('option'), { value: k, textContent: k }),
      ),
    );
    this.#select<HTMLSelectElement>('examples').append(
      ...examples.map((e) =>
        Object.assign(document.createElement('option'), { value: e.name, textContent: e.name }),
      ),
    );
    this.#wire();
  }

  /** Starts the app once the core is loaded. */
  async start(dd: DiaDraw): Promise<void> {
    this.dd = dd;
    this.querySelector('.app-version')!.textContent = dd.version;
    this.#palette.dd = dd;
    this.#text.diagnostics = (text) => {
      this.#sync(text);
      return this.#diagnostics;
    };
    const shared = await decodeSource(location.hash.slice(1));
    await this.#openStorage();
    // A share link wins; otherwise pick up where the last visit left off.
    const draft =
      shared === undefined ? await this.#library?.readDraft().catch(() => undefined) : undefined;
    if (shared !== undefined) this.#load(shared);
    else if (draft) await this.#restore(draft);
    else this.#load(FALLBACK);
    window.addEventListener('hashchange', async () => {
      const text = await decodeSource(location.hash.slice(1));
      if (text !== undefined) this.#load(text);
    });
  }

  // ── Wiring ──

  #select<T extends Element>(role: string): T {
    return this.querySelector<T>(`[data-role="${role}"]`)!;
  }

  #wire(): void {
    const actions: Record<string, () => void | Promise<void>> = {
      open: () => this.#open(),
      save: () => this.#save(),
      storage: () => this.#showStorage(),
      fit: () => this.#fit(),
      undo: () => this.#text.undo(),
      redo: () => this.#text.redo(),
      clear: () => this.#clearCanvas(),
      share: () => this.#share(),
      svg: () => this.#export('svg'),
      png: () => this.#export('png'),
      import: () => this.#import(),
      shapes: () => this.#togglePalette(),
      grid: () => {
        const grid = readGrid();
        this.#setGrid({ ...grid, show: !grid.show });
      },
      drawio: () => this.#exportPages('drawio'),
      bpmn: () => this.#exportPages('bpmn'),
      'report-bug': () => this.#openFeedback('bug'),
      feedback: () => this.#openFeedback('feedback'),
      about: () =>
        this.querySelector<AboutDialog>('dd-about')!.show(
          this.dd?.version ?? '',
          isTauri() ? 'desktop' : 'web',
        ),
    };
    this.addEventListener('dd-insert', (e) =>
      this.#insert((e as CustomEvent<InsertDetail>).detail),
    );
    this.addEventListener('dd-close', () => this.#togglePalette(false));
    this.#wireStorage();
    this.querySelector('header')!.addEventListener('click', (e) => {
      const action = (e.target as Element).closest('[data-action]')?.getAttribute('data-action');
      if (action) void actions[action]?.();
    });
    const gridSize = this.#select<HTMLSelectElement>('grid-size');
    gridSize.append(
      ...GRID_SIZES.map((n) =>
        Object.assign(document.createElement('option'), {
          value: String(n),
          textContent: `${n}`,
        }),
      ),
    );
    // Choosing a size is asking for the grid.
    gridSize.addEventListener('change', () =>
      this.#setGrid({ show: true, size: Number(gridSize.value) }),
    );
    this.#setGrid(readGrid());
    this.#select<HTMLSelectElement>('examples').addEventListener('change', (e) => {
      const select = e.target as HTMLSelectElement;
      const example = examples.find((x) => x.name === select.value);
      select.value = '';
      if (!example) return;
      this.#file = undefined;
      this.#watchFile();
      this.#viewId = undefined;
      history.replaceState(null, '', location.pathname);
      this.#load(example.source);
    });
    this.#select<HTMLSelectElement>('add').addEventListener('change', (e) => {
      const select = e.target as HTMLSelectElement;
      const kind = select.value;
      select.value = '';
      if (kind) this.#addElement(kind);
    });
    this.#select<HTMLSelectElement>('view-actions').addEventListener('change', (e) => {
      const select = e.target as HTMLSelectElement;
      const action = select.value;
      select.value = '';
      if (action === 'new' || action === 'rename' || action === 'remove') this.#viewAction(action);
    });
    this.#select<HTMLSelectElement>('views').addEventListener('change', (e) => {
      this.#viewId = (e.target as HTMLSelectElement).value;
      this.#render();
    });

    this.#text.addEventListener('dd-change', (e) => {
      this.#sync((e as CustomEvent<{ text: string }>).detail.text);
      this.#updateTitle();
      clearTimeout(this.#timer);
      this.#timer = setTimeout(() => this.#render(), 60);
    });

    const canvas = this.#canvas;
    canvas.nameOf = (id) => this.#model?.elements[id]?.name ?? id;
    canvas.labelOf = (id) => this.#model?.relationships[id]?.label ?? '';
    canvas.previewDrop = (id, box, freeform) => {
      if (!this.#session || this.#viewId === undefined) return undefined;
      try {
        return this.#session.dropHint(this.#viewId, id, box, freeform);
      } catch {
        return undefined;
      }
    };
    const on = <T>(type: string, handler: (detail: T) => void) =>
      canvas.addEventListener(type, (e) => handler((e as CustomEvent<T>).detail));
    on<{ selection: Selection }>('dd-select', ({ selection }) => {
      this.#reveal(selection);
      this.#inspect();
    });
    // While a field is typed in, the inspector outlines the ones the edit would refuse.
    this.#inspector.checkElement = (element, typed) => {
      const model = this.#model;
      const before = model?.elements[element.id];
      if (!this.#session || !model || !before) return {};
      const problems: Record<string, string> = {};
      const own = localId(before);
      const id = typed.id.trim();
      try {
        // The Id apart: its check is a dry run of the rename, kept while only other fields change.
        Object.assign(problems, this.#session.elementProblems(element, own));
        if (id !== own) {
          let check = this.#idCheck;
          if (check?.model !== model || check.element !== element.id || check.id !== id) {
            check = {
              model,
              element: element.id,
              id,
              problem: this.#session.elementProblems(before, id).id,
            };
            this.#idCheck = check;
          }
          if (check.problem !== undefined) problems.id = check.problem;
        }
      } catch {
        // An element the core cannot read back is refused on commit, with the reason.
      }
      // As #updateElement: shared styles count only when that field was edited.
      if (typed.sharedStyles !== sharedStyles(model, before)) {
        try {
          sharedStylesOperation(model, element, typed.sharedStyles);
        } catch (error) {
          problems.sharedStyles = (error as Error).message;
        }
      }
      return problems;
    };
    this.#inspector.checkRelationship = (relationship, name) => {
      try {
        return this.#session?.relationshipProblems(relationship, name) ?? {};
      } catch {
        return {};
      }
    };
    this.#inspector.addEventListener('dd-update', (e) => {
      const {
        element,
        sharedStyles: typed,
        rename,
      } = (e as CustomEvent<{ element: ModelElement; sharedStyles: string; rename?: string }>)
        .detail;
      if (rename === undefined) {
        this.#updateElement(element, typed);
        return;
      }
      // A new id first; the other fields then apply to the renamed element.
      const was = this.#model?.elements[element.id];
      const id = this.#renameElement(element.id, rename);
      const now = id === undefined ? undefined : this.#model?.elements[id];
      if (!was || !now) return;
      // A name left as it was shown follows the id when the text writes none.
      const name = element.name === was.name ? now.name : element.name;
      this.#updateElement({ ...element, id: now.id, name }, typed);
    });
    this.#stylePanel.addEventListener('dd-document-update', (e) => {
      const model = this.#model;
      if (!model) return;
      const { fields } = (e as CustomEvent<{ fields: DocumentFields }>).detail;
      try {
        const op = documentOperation(model, this.#viewId, fields);
        if (op) this.#apply(op);
      } catch (error) {
        this.#flash((error as Error).message);
      }
    });
    this.#inspector.addEventListener('dd-update-relationship', (e) => {
      const { relationship, name } = (
        e as CustomEvent<{ relationship: ModelRelationship; name: string | undefined }>
      ).detail;
      let id: string | undefined;
      this.#change(() => {
        id = this.#session!.updateRelationship(relationship, name).id;
      });
      // A new name is a new id: keep the line selected under it.
      if (id !== undefined && id !== relationship.id) {
        this.#canvas.selection = { elements: [], relationships: [id] };
        this.#inspect();
      }
    });
    on<{ id: string; box: Box; freeform: boolean }>('dd-drop', ({ id, box, freeform }) =>
      this.#change(() => this.#session!.drop(this.#viewId!, id, box, freeform)),
    );
    on<{ ids: string[]; offset: Point }>('dd-move', ({ ids, offset }) =>
      this.#change(() => this.#session!.move(this.#viewId!, ids, offset)),
    );
    on<{ id: string; box: Box }>('dd-resize', ({ id, box }) =>
      this.#change(() => this.#session!.resize(this.#viewId!, id, box)),
    );
    on<{ id: string }>('dd-fit', ({ id }) => this.#fit([id]));
    on<{ id: string; degrees: number }>('dd-rotate', ({ id, degrees }) =>
      this.#change(() => this.#session!.rotate(this.#viewId!, id, degrees)),
    );
    on<{ id: string; points: Point[] }>('dd-reroute', ({ id, points }) =>
      this.#change(() => this.#session!.reroute(this.#viewId!, id, points)),
    );
    on<{ source: string; target: string }>('dd-connect', ({ source, target }) =>
      this.#change(() => this.#session!.connect(source, target)),
    );
    on<{ id: string; end: 'source' | 'target'; element: string }>(
      'dd-reconnect',
      ({ id, end, element }) => {
        let after: string | undefined;
        this.#change(() => {
          after = this.#session!.reconnect(id, end, element)?.id;
        });
        // The line stays selected under its new id (a derived id follows its endpoints).
        if (after !== undefined) this.#canvas.selection = { elements: [], relationships: [after] };
        this.#inspect();
      },
    );
    on<{ id: string; name: string }>('dd-rename', ({ id, name }) =>
      this.#apply({ type: 'renameElement', id, name }),
    );
    on<{ id: string; label?: string }>('dd-relabel', ({ id, label }) =>
      this.#apply({ type: 'setRelationshipLabel', id, label }),
    );
    on('dd-rename-element', () => {
      const { elements, relationships } = this.#canvas.selection;
      this.#askRenameElement(
        elements.length === 1 && relationships.length === 0 ? elements[0] : undefined,
      );
    });
    this.#text.addEventListener('dd-rename-element', (e) => {
      const { offset } = (e as CustomEvent<{ offset: number }>).detail;
      this.#askRenameElement(this.#session?.elementAt(offset));
    });
    on<{ selection: Selection }>('dd-delete', ({ selection }) =>
      this.#change(() => this.#session!.remove(selection)),
    );
    on('dd-undo', () => this.#text.undo());
    on('dd-redo', () => this.#text.redo());

    window.addEventListener('keydown', (e) => {
      if (!(e.metaKey || e.ctrlKey)) return;
      if (e.altKey && e.code === 'Equal') {
        // ⌥⌘= / Ctrl+Alt+= sizes the selected elements to fit their content, as in the native apps.
        e.preventDefault();
        this.#fit();
        return;
      }
      if (e.key === "'") {
        // ⌘' / Ctrl+' shows or hides the grid, as in the native apps.
        e.preventDefault();
        const grid = readGrid();
        this.#setGrid({ ...grid, show: !grid.show });
        return;
      }
      if (e.key.toLowerCase() === 's') {
        e.preventDefault();
        void this.#save(e.shiftKey);
      } else if (e.key.toLowerCase() === 'o') {
        e.preventDefault();
        void this.#open();
      }
    });
  }

  // ── Document ──

  /** Loads a new document: a fresh editor history, layouts and selection. */
  #load(text: string): void {
    this.#session?.dispose();
    const session = this.dd!.createSession(text);
    this.#session = session;
    // The session reports every change, whoever made it: typing, an undo, or a canvas gesture.
    session.on('change', (change) => this.#changed(change));
    session.on('libraries', () => this.#render());
    this.#canvas.selection = { elements: [], relationships: [] };
    this.#adopt(session.state());
    this.#text.load(text);
    this.#updateTitle();
    this.#render();
  }

  /**
   * Clear Canvas: the whole text becomes an empty diagram as one change in the shared undo
   * history. With unsaved changes it asks first.
   */
  #clearCanvas(): void {
    const action = clearAction(this.#text.text, this.#file?.saved);
    if (action === 'nothing') return;
    if (
      action === 'ask' &&
      !confirm(
        'Clear the canvas? Everything in this diagram is removed, including changes that are not saved. Undo brings it back.',
      )
    )
      return;
    this.#canvas.selection = { elements: [], relationships: [] };
    this.#text.replace(EMPTY_DIAGRAM, 'input.clear');
    this.#inspect();
  }

  /** Brings the session to the editor's text (after typing or an undo). */
  #sync(text: string): void {
    // After a canvas change the session is already at this text; after typing it is not, and
    // setting it reports the change.
    if (this.#session && text !== this.#synced) this.#session.setSource(text);
  }

  /** The session changed: take its state, and bring the editor along when a gesture made it. */
  #changed(change: SessionChange): void {
    this.#adopt(change.state);
    const update = change.update;
    const before = this.#text.text;
    if (!update || update.source === before) return;
    // The editor follows the session as one transaction (it then reports the text we have).
    this.#text.applyEdits(
      update.edits.length ? update.edits : [{ from: 0, to: before.length, insert: update.source }],
    );
    if (!update.minimal) this.#flash('Reformatted the file to apply that change');
  }

  #adopt(state: SessionState): void {
    this.#model = state.model;
    this.#diagnostics = state.diagnostics;
    this.#views = state.views.map((v) => v.id);
    this.#synced = this.#session?.source;
    this.#inspect();
    void this.#loadLibraries();
  }

  /**
   * The properties panel, always beside the canvas, shows the one selected element or
   * relationship, as the model has it now; with nothing selected, the document's theme and
   * styles; with several, a note saying so.
   */
  #inspect(): void {
    const { elements, relationships } = this.#canvas.selection;
    this.querySelector<HTMLButtonElement>('[data-action="fit"]')!.disabled = elements.length === 0;
    const model = this.#model;
    const [line] = relationships;
    const relationship =
      line !== undefined && relationships.length === 1 && elements.length === 0
        ? model?.relationships[line]
        : undefined;
    if (relationship) {
      this.#inspector.relationship = relationship;
    } else {
      const id = elements.length === 1 && relationships.length === 0 ? elements[0] : undefined;
      const element = id === undefined ? undefined : model?.elements[id];
      this.#inspector.sharedStyles = model && element ? sharedStyles(model, element) : '';
      this.#inspector.element = element;
    }
    const selected = elements.length + relationships.length;
    const choices = this.#choices();
    this.#inspector.choices = choices;
    this.#stylePanel.themes = (model?.themes ?? []).map((t) => t.name);
    this.#stylePanel.selectors = selectorChoices(choices);
    this.#stylePanel.kinds = choices.kinds.map((k) => k.value);
    this.#stylePanel.fields =
      model && selected === 0 ? documentFields(model, this.#viewId) : undefined;
    const note = this.querySelector<HTMLElement>('.properties-note')!;
    note.hidden = !this.#inspector.hidden || !this.#stylePanel.hidden;
    note.textContent =
      selected > 1
        ? `${selected} items selected. Select one element or relationship to edit its properties.`
        : 'Nothing to edit here.';
  }

  /** What the inspector's pickers offer; the libraries' items are asked for when they change. */
  #choices(): FieldChoices {
    const model = this.#model;
    const libraries = JSON.stringify(model?.libraries ?? []);
    if (this.dd && model && libraries !== this.#paletteFor) {
      this.#paletteFor = libraries;
      try {
        this.#libraryItems = this.dd.palette(model).items;
      } catch {
        this.#libraryItems = [];
      }
    }
    return fieldChoices(model, this.#libraryItems);
  }

  /** An inspector edit: the element's own properties and the style blocks that apply to it, as one undo step. */
  #updateElement(element: ModelElement, typedStyles: string): void {
    const model = this.#model;
    const before = model?.elements[element.id];
    if (!model || !before) return;
    const operations: Operation[] = [];
    if (JSON.stringify(element) !== JSON.stringify(before))
      operations.push({ type: 'updateElement', element });
    // Shared styles only when that field was edited, so a kind or tag change alone never touches them.
    if (typedStyles !== sharedStyles(model, before)) {
      try {
        const op = sharedStylesOperation(model, element, typedStyles);
        if (op) operations.push(op);
      } catch (error) {
        this.#flash((error as Error).message);
        return;
      }
    }
    if (operations.length === 0) return;
    this.#apply(operations.length === 1 ? operations[0]! : { type: 'batch', operations });
  }

  /** draw.io stencil sets the document uses are fetched, then the canvas draws them. */
  async #loadLibraries(): Promise<void> {
    const dd = this.dd;
    const wanted = (this.#model?.libraries ?? [])
      .map((u) => u.name)
      .filter((name) => isDrawioLibrary(name) && !drawioSetRequested(name));
    if (!dd || wanted.length === 0) return;
    const loaded = await Promise.all(wanted.map((name) => loadDrawioSet(dd, name)));
    // The session reports `libraries`, and the canvas draws them.
    if (loaded.some(Boolean)) this.#session?.refreshLibraries();
  }

  /** Runs a session change; the editor follows through the session's `change` event. */
  /** Sizes elements (by default the selected ones) to fit their content in the current view. */
  #fit(ids: readonly string[] = this.#canvas.selection.elements): void {
    if (!this.#session || this.#viewId === undefined || ids.length === 0) return;
    this.#change(() => this.#session!.fit(this.#viewId!, [...ids]));
  }

  #change(run: () => void): void {
    try {
      run();
    } catch (error) {
      if (error instanceof DiaDrawError) this.#flash(error.message);
      else throw error;
    }
  }

  #apply(operation: Operation): void {
    this.#change(() => this.#session!.apply(operation));
  }

  #addElement(kind: string): void {
    const view = this.#viewId === undefined ? undefined : this.#model?.views[this.#viewId];
    let created: string | undefined;
    this.#change(() => {
      created = this.#session!.create(
        kind,
        view?.scope === undefined ? {} : { parent: view.scope },
      ).id;
    });
    if (created) this.#canvas.selection = { elements: [created], relationships: [] };
    this.#inspect();
  }

  /**
   * Renames an element (its id) and every reference to it; the canvas keeps it selected under
   * its new id. Returns the id afterwards, or undefined when the rename was refused.
   */
  #renameElement(id: string, to: string): string | undefined {
    let renamed: string | undefined;
    this.#change(() => {
      renamed = this.#session!.renameElement(id, to).id;
    });
    if (renamed !== undefined && renamed !== id) {
      const selection = this.#canvas.selection;
      const relationships = this.#model?.relationships ?? {};
      this.#canvas.selection = {
        elements: selection.elements.map((e) =>
          e === id || e.startsWith(`${id}.`) ? renamed + e.slice(id.length) : e,
        ),
        // A line whose id is derived from its ends gets a new id with them.
        relationships: selection.relationships.filter((r) => r in relationships),
      };
      this.#inspect();
    }
    return renamed;
  }

  /**
   * Rename Element (F2): asks for a new name for the element named at the text cursor, or the one
   * selected on the canvas, then renames it everywhere.
   */
  #askRenameElement(id: string | undefined): void {
    if (id === undefined || !this.#model?.elements[id]) {
      this.#flash("Put the cursor on an element's name, or select one element, to rename it");
      return;
    }
    const to = prompt(
      `Rename ${id} to (letters, digits and _):`,
      id.slice(id.lastIndexOf('.') + 1),
    );
    if (to !== null) this.#renameElement(id, to);
  }

  /** New, Rename or Remove View: asks for a name where one is needed, then edits the text. */
  #viewAction(action: 'new' | 'rename' | 'remove'): void {
    const session = this.#session;
    const current = this.#viewId;
    if (!session || current === undefined) return;
    let name: string | null = null;
    if (action !== 'remove') {
      name = prompt(
        action === 'new' ? 'New view name (letters, digits and _):' : 'Rename the view to:',
        action === 'new' ? session.freshViewId() : current,
      );
      if (name === null) return;
    }
    let applied = false;
    this.#change(() => {
      const scope = this.#model?.views[current]?.scope;
      if (action === 'new') session.addView(name!, scope);
      else if (action === 'rename') session.renameView(current, name!);
      else session.removeView(current);
      applied = true;
    });
    // Show the new or renamed view (the core trims the name); after a removal #render moves to
    // the first view left.
    if (applied && name !== null) this.#viewId = name.trim();
    this.#render();
  }

  /** Selecting one element on the canvas moves the text cursor to its declaration. */
  #reveal(selection: Selection): void {
    const id = selection.elements.length === 1 ? selection.elements[0] : undefined;
    if (id === undefined || !this.dd) return;
    const span = this.dd.parse(this.#text.text).sourceMap.elements[id];
    if (span) this.#text.select(span.name.from, span.name.to);
  }

  // ── Rendering ──

  #render(): void {
    const session = this.#session;
    if (!session) return;
    const views = this.#views;
    if (this.#viewId === undefined || !views.includes(this.#viewId)) this.#viewId = views[0];
    const picker = this.#select<HTMLSelectElement>('views');
    const options = views
      .map((v) => `<option value="${v}"${v === this.#viewId ? ' selected' : ''}>view ${v}</option>`)
      .join('');
    if (picker.innerHTML !== options) picker.innerHTML = options;
    picker.disabled = views.length <= 1; // nothing to choose between with one view
    const remove =
      this.#select<HTMLSelectElement>('view-actions').querySelector<HTMLOptionElement>(
        'option[value="remove"]',
      );
    if (remove) remove.disabled = views.length <= 1; // a document keeps one view
    let layoutError: string | undefined;
    if (this.#viewId !== undefined) {
      try {
        this.#rendered = session.render(this.#viewId);
      } catch (error) {
        layoutError = error instanceof Error ? error.message : String(error);
      }
    }
    this.#canvas.view = this.#rendered;
    this.#showMessages(layoutError);
    this.#inspect();
  }

  #showMessages(layoutError?: string): void {
    const errors = this.#diagnostics.filter((d) => d.severity === 'error');
    const messages: string[] = [];
    if (errors.length > 0) {
      const first = errors[0]!;
      messages.push(
        `<p class="error" role="alert">${errors.length} error${errors.length > 1 ? 's' : ''}, first on line ${first.line}: ${escape(first.message)}. The diagram shows what could be read.</p>`,
      );
    }
    if (layoutError) messages.push(`<p class="error">Layout failed: ${escape(layoutError)}</p>`);
    const ignored = this.#rendered?.ignoredHints.length ?? 0;
    if (ignored > 0)
      messages.push(
        `<p class="warning">${ignored} layout hint${ignored > 1 ? 's' : ''} could not be honoured.</p>`,
      );
    this.querySelector('.messages')!.innerHTML = messages.join('');
  }

  /** What the last import could not carry over, until dismissed. */
  #showReport(): void {
    const slot = this.querySelector('.report-slot')!;
    const r = this.#report;
    if (!r || (r.dropped.length === 0 && r.notes.length === 0)) {
      slot.innerHTML = '';
      return;
    }
    const items = [
      ...r.dropped.map(
        (d) => `<li>${d.line === undefined ? '' : `line ${d.line}: `}${escape(d.message)}</li>`,
      ),
      ...r.notes.map((n) => `<li>${escape(n)}</li>`),
    ];
    slot.innerHTML = `<details class="report"><summary>Imported ${escape(r.file)}: ${r.dropped.length} item${r.dropped.length === 1 ? '' : 's'} not carried over <button type="button" data-dismiss>Dismiss</button></summary><ul>${items.join('')}</ul></details>`;
    slot.querySelector('[data-dismiss]')!.addEventListener('click', (e) => {
      e.preventDefault();
      this.#report = undefined;
      this.#showReport();
    });
  }

  // ── Files, sharing, export ──

  #updateTitle(): void {
    const file = this.#file;
    const dirty = file !== undefined && this.#text.text !== file.saved;
    const stored = this.#stored;
    const where = stored && this.#locationOf(stored.location);
    const place = where ? (where.id === 'browser' ? 'this browser' : where.label) : '';
    const label = this.querySelector('.file-name')!;
    label.textContent = file
      ? ` · ${file.name}${place ? ` (in ${place})` : ''}${dirty ? ' •' : ''}`
      : '';
    label.setAttribute('title', place ? `Saved in ${place}; ⌘S saves it there` : '');
    // Every change of text or file ends here: keep the working copy for the next visit.
    this.#scheduleDraft();
  }

  #flash(text: string): void {
    const status = this.querySelector('.status')!;
    status.textContent = text;
    clearTimeout(this.#statusTimer);
    this.#statusTimer = setTimeout(() => (status.textContent = ''), 2500);
  }

  async #open(): Promise<void> {
    const opened = await openFile(['.draw']);
    if (!opened) return;
    this.#file = {
      name: opened.name,
      saved: opened.text,
      ...(opened.handle && { handle: opened.handle }),
    };
    this.#watchFile();
    history.replaceState(null, '', location.pathname);
    this.#viewId = undefined;
    this.#report = undefined;
    this.#showReport();
    this.#load(opened.text);
  }

  async #import(): Promise<void> {
    const dd = this.dd;
    if (!dd) return;
    const opened = await openFile([
      '.mmd',
      '.mermaid',
      '.puml',
      '.plantuml',
      '.dsl',
      '.json',
      '.drawio',
      '.xml',
      '.svg',
      '.png',
      '.vsdx',
      '.bpmn',
    ]);
    if (!opened) return;
    // draw.io files draw their stencils with the drawio-* libraries' artwork once the index is in.
    const binary = /\.(png|svg|vsdx)$/i.test(opened.name);
    const stencils =
      /\.(drawio|dio|xml|svg|png|vsdx)$/i.test(opened.name) &&
      (await drawioIndex(dd)) !== undefined;
    try {
      const { model, report } = dd.importDiagram(binary ? opened.bytes : opened.text, opened.name, {
        stencils,
      });
      this.#file = { name: opened.name.replace(/\.[^.]+$/, '.draw'), saved: '' };
      this.#watchFile();
      this.#report = { ...report, file: opened.name };
      this.#viewId = undefined;
      history.replaceState(null, '', location.pathname);
      this.#load(dd.print(model));
      this.#showReport();
    } catch (error) {
      if (error instanceof DiaDrawError) this.#flash(error.message);
      else throw error;
    }
  }

  /** Opens the issue form for `kind` in a new tab, with the app, version and system filled in. */
  #openFeedback(kind: FeedbackKind): void {
    const url = feedbackUrl(kind, {
      app: isTauri() ? 'Desktop (Tauri)' : 'Web',
      version: this.dd?.version ?? '',
      os: describeSystem(navigator.userAgent),
    });
    window.open(url, '_blank', 'noopener');
  }

  /** Shows or hides the grid, remembers the choice and updates the toolbar. */
  #setGrid(grid: { show: boolean; size: number }): void {
    writeGrid(grid);
    this.#canvas.grid = shownStep(grid);
    this.querySelector('[data-action="grid"]')!.setAttribute('aria-pressed', String(grid.show));
    const sizes = this.#select<HTMLSelectElement>('grid-size');
    if (![...sizes.options].some((o) => o.value === String(grid.size)))
      sizes.append(
        Object.assign(document.createElement('option'), {
          value: String(grid.size),
          textContent: `${grid.size}`,
        }),
      );
    sizes.value = String(grid.size);
  }

  #togglePalette(open = this.#palette.hidden): void {
    this.#palette.hidden = !open;
    this.querySelector('[data-action="shapes"]')!.setAttribute('aria-pressed', String(open));
  }

  /** Adds a palette item: its library's `use` if missing, then the shape, the kind or the fragment's text. */
  #insert({ item, library }: InsertDetail): void {
    const dd = this.dd;
    const session = this.#session;
    if (!dd || !session || this.#viewId === undefined) return;
    let model = this.#model;
    let alias = model?.libraries?.find((u) => u.name === library.name)?.alias;
    if (!alias) {
      alias = item.alias;
      for (let i = 2; model?.libraries?.some((u) => u.alias === alias); i++)
        alias = `${item.alias}${i}`;
      const use = { name: library.name, range: `^${library.version.split('.')[0]}`, alias };
      this.#apply({ type: 'useLibrary', use });
      model = this.#model;
    }
    if (item.type === 'fragment') {
      // Parameters that would clash with existing element ids get a number.
      const values: Record<string, string> = {};
      const current = model;
      const fragment = dd
        .palette(current!)
        .items.find((i) => i.type === 'fragment' && i.alias === alias && i.id === item.id);
      if (!fragment || !current) return;
      const text = dd.expandFragment(current, alias, item.id, values);
      const before = this.#text.text;
      const body = dd.parse(before).sourceMap.model;
      const indented = text
        .split('\n')
        .map((l) => (l ? `  ${l}` : l))
        .join('\n');
      let after: string;
      if (body) {
        const at = before.lastIndexOf('}', body.to);
        after = `${before.slice(0, at).replace(/\s*$/, '\n')}${indented}\n${before.slice(at)}`;
      } else after = `${before.replace(/\s*$/, '\n\n')}model {\n${indented}\n}\n`;
      this.#text.applyEdits([{ from: 0, to: before.length, insert: after }]);
      return;
    }
    const scope = model?.views[this.#viewId]?.scope;
    // A library kind is written under the alias the document uses for its library.
    const own = `${item.alias}.`;
    const kind =
      item.type === 'kind'
        ? `${alias}.${item.id}`
        : item.kind?.startsWith(own)
          ? `${alias}.${item.kind.slice(own.length)}`
          : item.kind;
    let created: string | undefined;
    this.#change(() => {
      created = session.create(kind ?? 'component', {
        name: item.label,
        ...(item.type !== 'kind' && { shape: `${alias}.${item.id}` }),
        ...(scope !== undefined && { parent: scope }),
      }).id;
    });
    if (created) this.#canvas.selection = { elements: [created], relationships: [] };
    this.#inspect();
  }

  /** Every view as a page of a draw.io or BPMN file. */
  #exportPages(format: 'drawio' | 'bpmn'): void {
    const dd = this.dd;
    const session = this.#session;
    const model = this.#model;
    if (!dd || !session || !model) return;
    const pages: ExportPage[] = [];
    for (const id of this.#views) {
      try {
        const r = session.render(id);
        pages.push({ view: id, nodes: r.nodes, edges: r.edges });
      } catch {
        // A view that does not lay out is left out.
      }
    }
    const xml = format === 'drawio' ? dd.exportDrawio(model, pages) : dd.exportBpmn(model, pages);
    const name = `${(this.#file?.name ?? 'diagram.draw').replace(/\.draw$/, '')}.${format}`;
    download(new Blob([xml], { type: 'application/xml' }), name);
  }

  /** In the desktop app, reloads the file when it changes on disk and there are no unsaved edits. */
  #watchFile(): void {
    this.#stopWatching?.();
    this.#stopWatching = undefined;
    const handle = this.#file?.handle;
    if (!handle?.watch) return;
    const current = () => this.#file?.handle === handle;
    void handle
      .watch((text) => {
        const file = this.#file;
        if (!file || !current() || this.#text.text !== file.saved || text === file.saved) return;
        this.#file = { ...file, saved: text };
        this.#load(text);
      })
      .then((stop) => {
        if (current()) this.#stopWatching = stop;
        else stop();
      });
  }

  disconnectedCallback(): void {
    this.#stopWatching?.();
  }

  async #save(as = false): Promise<void> {
    const text = this.#text.text;
    const file = this.#file;
    if (file?.handle && !as) {
      try {
        await file.handle.save(text);
      } catch (error) {
        const stored = (file.handle as StoredHandle).stored;
        // A cloud save after the sign-in ran out: sign in from the dialog (popups need a click).
        if (error instanceof SignInRequired && stored) {
          await this.#showStorage(stored.location, `${error.message}, then press ⌘S again.`);
        } else {
          this.#flash(
            `Could not save ${file.name}: ${error instanceof Error ? error.message : String(error)}`,
          );
        }
        return;
      }
      this.#file = { ...file, saved: text };
      this.#flash(`Saved ${file.name}`);
    } else {
      const handle = await saveFileAs(text, file?.name ?? 'diagram.draw');
      this.#file = {
        name: handle?.name ?? file?.name ?? 'diagram.draw',
        saved: text,
        ...(handle && { handle }),
      };
      this.#watchFile();
      if (handle) this.#flash(`Saved ${handle.name}`);
    }
    this.#updateTitle();
  }

  // ── Diagrams in this browser, Google Drive and OneDrive ──

  /** Opens the browser's diagram library (left out where IndexedDB is unavailable) and the clouds. */
  async #openStorage(): Promise<void> {
    try {
      this.#library = await DiagramLibrary.open();
    } catch {
      this.#library = undefined;
    }
    const clouds = browserClouds();
    this.#locations = [
      ...(this.#library ? [browserLocation(this.#library)] : []),
      cloudLocation(clouds.gdrive),
      cloudLocation(clouds.onedrive),
    ];
    this.#storagePanel.locations = this.#locations;
  }

  #locationOf(id: LocationId): StorageLocation | undefined {
    return this.#locations.find((l) => l.id === id);
  }

  /** Where the document is stored, when it was opened from or saved to a storage location. */
  get #stored(): { location: LocationId; id: string } | undefined {
    return (this.#file?.handle as StoredHandle | undefined)?.stored;
  }

  /** A file handle whose saves go to diagram `id` in `location`. */
  #storedHandle(location: StorageLocation, id: string, name: string): StoredHandle {
    return {
      name: editorFileName(name),
      stored: { location: location.id, id },
      save: async (text) => {
        await location.save(id, text, plainName(name));
      },
    };
  }

  /** Makes a stored diagram the document being edited. */
  #setStored(location: StorageLocation, id: string, name: string, saved: string): void {
    this.#file = {
      name: editorFileName(name),
      handle: this.#storedHandle(location, id, name),
      saved,
    };
    this.#watchFile();
  }

  #wireStorage(): void {
    const panel = this.#storagePanel;
    const on = <T extends { location: LocationId }>(
      type: string,
      handler: (detail: T, location: StorageLocation) => void | Promise<void>,
    ) =>
      panel.addEventListener(type, (e) => {
        const detail = (e as CustomEvent<T>).detail;
        const location = this.#locationOf(detail.location);
        if (!location) return;
        void Promise.resolve(handler(detail, location)).catch(async (error: unknown) => {
          if (error instanceof SignInRequired) {
            await panel.refresh();
            return;
          }
          const message = error instanceof Error ? error.message : String(error);
          if (panel.isOpen)
            await panel.show(plainName(this.#file?.name ?? 'Untitled'), location.id, message);
          else this.#flash(`${location.label}: ${message}`);
        });
      });
    on<{ location: LocationId; name: string }>('dd-storage-save', async ({ name }, location) => {
      const text = this.#text.text;
      const saved = await location.create(name, text);
      this.#setStored(location, saved.id, saved.name, text);
      this.#updateTitle();
      panel.close();
      this.#flash(
        `Saved ${plainName(saved.name)} in ${location.id === 'browser' ? 'this browser' : location.label}`,
      );
    });
    on<{ location: LocationId; id: string }>('dd-storage-open', async ({ id }, location) => {
      const diagram = await location.read(id);
      if (!diagram) {
        this.#flash(
          `That diagram is no longer in ${location.id === 'browser' ? 'this browser' : location.label}`,
        );
        await panel.refresh();
        return;
      }
      this.#setStored(location, id, diagram.file.name, diagram.text);
      history.replaceState(null, '', window.location.pathname);
      this.#viewId = undefined;
      this.#report = undefined;
      this.#showReport();
      panel.close();
      this.#load(diagram.text);
    });
    on<{ location: LocationId; id: string; name: string }>(
      'dd-storage-renamed',
      ({ id, name }, location) => {
        const file = this.#file;
        const stored = this.#stored;
        if (!file || stored?.location !== location.id || stored.id !== id) return;
        this.#file = {
          ...file,
          name: editorFileName(name),
          handle: this.#storedHandle(location, id, name),
        };
        this.#updateTitle();
      },
    );
    on<{ location: LocationId; id: string }>('dd-storage-removed', ({ id }, location) => {
      const file = this.#file;
      const stored = this.#stored;
      if (!file || stored?.location !== location.id || stored.id !== id) return;
      // The text stays in the editor, unsaved: saving again asks where.
      this.#file = { name: file.name, saved: '' };
      this.#updateTitle();
      this.#flash(`Deleted ${file.name}`);
    });
    // Write the working copy right away when the tab is hidden, closed or reloaded.
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'hidden') this.#writeDraft();
    });
  }

  async #showStorage(location?: LocationId, message?: string): Promise<void> {
    const panel = this.#storagePanel;
    panel.editing = this.#stored;
    await panel.show(
      plainName(this.#file?.name ?? 'Untitled'),
      location ?? this.#stored?.location,
      message,
    );
  }

  /** Picks up the working copy saved by the last visit. */
  async #restore(draft: Draft): Promise<void> {
    let file: OpenDocument | undefined;
    const where: LocationId | undefined =
      draft.location ?? (draft.libraryId !== undefined ? 'browser' : undefined);
    const id = draft.fileId ?? draft.libraryId;
    const location = where && this.#locationOf(where);
    if (location && id !== undefined && draft.name !== undefined) {
      // Saves go back to where the diagram came from. Its text there is not read again (a cloud
      // needs a sign-in for that); the working copy remembers what was last saved.
      file = {
        name: draft.name,
        handle: this.#storedHandle(location, id, draft.name),
        saved: draft.saved ?? '',
      };
      // A browser diagram deleted meanwhile (in another tab) is no longer where it was.
      if (where === 'browser' && !(await this.#library?.get(id).catch(() => undefined))) {
        file = { name: draft.name, saved: '' };
      }
    } else if (draft.name !== undefined) {
      // A file from disk: its handle does not survive a reload, so saving asks where again.
      file = { name: draft.name, saved: draft.saved ?? '' };
    }
    this.#file = file;
    this.#load(draft.text);
    if (file || draft.text !== FALLBACK) this.#flash('Restored your last diagram');
  }

  #scheduleDraft(): void {
    if (!this.#library) return;
    clearTimeout(this.#draftTimer);
    this.#draftTimer = setTimeout(() => this.#writeDraft(), 400);
  }

  #writeDraft(): void {
    clearTimeout(this.#draftTimer);
    const library = this.#library;
    if (!library || !this.#session) return;
    const file = this.#file;
    const stored = this.#stored;
    const draft: Draft = {
      text: this.#text.text,
      ...(file && { name: file.name, saved: file.saved }),
      ...(stored && { location: stored.location, fileId: stored.id }),
    };
    library.writeDraft(draft).catch(() => {
      // Out of quota or storage cleared: the working copy is a convenience, editing goes on.
    });
  }

  async #share(): Promise<void> {
    history.replaceState(null, '', `#${await encodeSource(this.#text.text)}`);
    try {
      await navigator.clipboard.writeText(location.href);
      this.#flash('Link copied');
    } catch {
      this.#flash('Link is in the address bar');
    }
  }

  async #export(format: 'svg' | 'png'): Promise<void> {
    if (!this.#session || this.#viewId === undefined) return;
    const svg = this.#session.exportSvg(this.#viewId);
    const blob =
      format === 'svg' ? new Blob([svg], { type: 'image/svg+xml' }) : await svgToPng(svg);
    download(blob, `${this.#viewId}.${format}`);
  }
}

/** The file being edited: its name, where saves go, and its text as last opened or saved. */
interface OpenDocument {
  name: string;
  handle?: FileHandle;
  saved: string;
}

/** A file handle for a diagram in this browser, Google Drive or OneDrive. */
interface StoredHandle extends FileHandle {
  readonly stored: { readonly location: LocationId; readonly id: string };
}

function escape(text: string): string {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

customElements.define('dd-app', DiaDrawApp);

declare global {
  interface HTMLElementTagNameMap {
    'dd-app': DiaDrawApp;
  }
}
