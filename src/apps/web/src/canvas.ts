import type { Box, LayoutHint, Point, RenderedView, Selection } from '@diadraw/wasm';
import {
  center,
  corners,
  distanceToSegment,
  gridLines,
  inside,
  insideTurned,
  resizedBox,
  rotateHandle,
  rotatedBounds,
  rotatePoint,
  snap,
  turned,
} from './canvas-geometry';

/**
 * `<dd-canvas>`: the diagram as an editor. Select (Shift or ⌘/Ctrl adds an item or takes it out),
 * drag over empty canvas to select what the band wholly encloses (with Shift or ⌘/Ctrl, adds it),
 * ⌘A / Ctrl+A selects everything, drag to move (Alt: place by the neighbours; dragging one of
 * several selected nodes moves them all, each pinned where it lands, and a click on one of them
 * without a drag selects it alone), drag the handle to connect, drag an end of a selected line to
 * reconnect it, double-click to rename (a name may take several lines: Enter starts a new one,
 * ⌘/Ctrl+Enter commits), Delete to remove, ⌘Z / ⇧⌘Z.
 * Drag a selected node's corner to resize it (Shift keeps its proportions; double-click it to fit
 * the node to its content) and the round handle off its top-right corner to turn it (snapping to
 * 15° steps within 5°). Outside `layout fixed` views only leaves resize (layouts size containers),
 * and only leaves turn, never in sequence views. In a fixed view, drag a selected relationship's
 * bend point to move it (a segment's middle adds one; dropping one on the straight line between
 * its neighbours removes it). The handles follow the shared C++ canvas (src/canvas).
 * With a grid shown (`grid`, a step in diagram units), what a gesture writes lines up with it: a
 * plain move drops the box's top-left corner on the nearest grid point, a resize in a fixed view
 * puts the dragged corner on one (elsewhere, and on a turned node, the size becomes whole grid
 * steps), and a bend point lands on one. Alt drops still go by the neighbours; turns keep their
 * 15° steps; a Shift resize keeps the proportions and ignores the grid.
 *
 * The canvas holds no model logic: it shows what the core rendered and reports gestures as
 * events, which the app turns into core calls:
 *   dd-select { selection } · dd-drop { id, box, freeform } · dd-connect { source, target }
 *   dd-move { ids, offset } (several nodes dragged together)
 *   dd-reconnect { id, end, element }
 *   dd-rename { id, name } · dd-relabel { id, label } · dd-delete { selection } · dd-undo · dd-redo ·
 *   dd-rename-element (F2: rename the selected element's id)
 *   dd-resize { id, box } (unturned; fixed views pin it there, others keep the size) · dd-fit { id }
 *   dd-rotate { id, degrees } · dd-reroute { id, points }  (fixed views; `points` are all the bends)
 */

type Gesture =
  | { kind: 'idle' }
  /** `narrow`: a plain press on one of several selected items; let go without a drag, it selects that node alone. */
  | { kind: 'press'; id: string; start: Point; narrow: boolean }
  /** A band dragged over empty canvas; `extend`: Shift or ⌘/Ctrl adds what it encloses. */
  | { kind: 'band'; start: Point; at: Point; moved: boolean; extend: boolean }
  | { kind: 'move'; id: string; start: Point; delta: Point; freeform: boolean }
  | { kind: 'connect'; from: string; to: Point; target?: string }
  /** `handle`: the corner held, clockwise from the top left. */
  | {
      kind: 'resize';
      id: string;
      handle: number;
      start: Point;
      at: Point;
      moved: boolean;
      keep: boolean;
    }
  | { kind: 'rotate'; id: string; start: Point; at: Point; moved: boolean }
  /** `handle`: the route point held, or with `insert`, the segment whose middle was held. */
  | {
      kind: 'bend';
      id: string;
      handle: number;
      insert: boolean;
      start: Point;
      at: Point;
      moved: boolean;
    }
  | { kind: 'reconnect'; id: string; end: End; to: Point; target?: string };

type End = 'source' | 'target';

const DRAG_THRESHOLD = 4;
/** Two presses on a resize corner within this many milliseconds are a double-click. */
const DOUBLE_TAP_MS = 500;
const GRIP = 4.5; // half a resize handle's side
const ROTATE_REACH = 17; // from a node's top-right corner to its rotate handle (pixels)
const STRAIGHTEN = 6; // a bend this near its neighbours' line goes
const SVG_NS = 'http://www.w3.org/2000/svg';

const STYLE = `
  :host { display: block; position: relative; overflow: auto; outline: none; padding: 1rem; box-sizing: border-box; }
  svg { display: block; user-select: none; touch-action: none; overflow: visible; }
  svg.gridded .dd-background { visibility: hidden; }
  .backdrop { pointer-events: none; }
  svg .dd-node { cursor: grab; }
  svg.dragging, svg.dragging .dd-node { cursor: grabbing; }
  svg .dd-edge { cursor: pointer; }
  .overlay .sel-node { fill: none; stroke: #2563eb; stroke-width: 2; stroke-dasharray: 5 3; pointer-events: none; }
  .overlay .sel-edge { fill: none; stroke: #2563eb; stroke-width: 4; stroke-opacity: 0.45; pointer-events: none; }
  .overlay .ghost, .overlay .connect-target { fill: #2563eb; fill-opacity: 0.12; stroke: #2563eb; stroke-width: 1.5; stroke-dasharray: 4 3; pointer-events: none; }
  .overlay .hint-label { font: 600 12px Inter, system-ui, sans-serif; fill: #1d4ed8; pointer-events: none; }
  .overlay .connect-line { stroke: #2563eb; stroke-width: 1.5; stroke-dasharray: 4 3; pointer-events: none; }
  .overlay .dd-handle, .overlay .dd-end { fill: #ffffff; stroke: #2563eb; stroke-width: 2; cursor: crosshair; }
  .overlay .dd-resize, .overlay .dd-bend { fill: #ffffff; stroke: #2563eb; stroke-width: 1.5; }
  .overlay .dd-resize[data-handle="0"], .overlay .dd-resize[data-handle="2"] { cursor: nwse-resize; }
  .overlay .dd-resize[data-handle="1"], .overlay .dd-resize[data-handle="3"] { cursor: nesw-resize; }
  .overlay .dd-rotate { fill: #ffffff; stroke: #2563eb; stroke-width: 1.5; cursor: grab; }
  .overlay .stalk { stroke: #2563eb; stroke-width: 1.5; pointer-events: none; }
  .overlay .dd-bend { cursor: move; }
  .overlay .dd-bend.add { opacity: 0.6; }
  .overlay .band { fill: #2563eb; fill-opacity: 0.08; stroke: #2563eb; stroke-opacity: 0.8; stroke-width: 1; pointer-events: none; }
  .overlay .bent { fill: none; stroke: #2563eb; stroke-width: 1.5; stroke-dasharray: 4 3; pointer-events: none; }
  .inline-edit { position: absolute; font: 13px Inter, system-ui, sans-serif; line-height: 18px; padding: 4px 6px; border: 2px solid #2563eb; border-radius: 4px; box-sizing: border-box; }
  textarea.inline-edit { resize: none; overflow: hidden; white-space: pre-wrap; }
`;

export class DiagramCanvas extends HTMLElement {
  #view: RenderedView | undefined;
  #selection: Selection = { elements: [], relationships: [] };
  #gesture: Gesture = { kind: 'idle' };
  /** The last press on a resize corner that did not drag: a second one soon after fits the node. */
  #gripTap: { id: string; time: number } | undefined;
  #svg: SVGSVGElement;
  #scene: SVGGElement;
  #overlay: SVGGElement;
  #editor: HTMLInputElement | HTMLTextAreaElement | undefined;
  #backdrop: SVGGElement;
  #grid = 0;

  /** Element names and relationship labels, for inline editing and hint previews. */
  nameOf: (id: string) => string = (id) => id;
  labelOf: (id: string) => string = () => '';
  /** The hint a drop would write, for the preview while dragging. */
  previewDrop: (id: string, box: Box, freeform: boolean) => LayoutHint | undefined = () =>
    undefined;

  constructor() {
    super();
    const root = this.attachShadow({ mode: 'open' });
    root.innerHTML = `<style>${STYLE}</style>`;
    this.tabIndex = 0;
    this.setAttribute('aria-label', 'Diagram canvas');
    this.#svg = document.createElementNS(SVG_NS, 'svg');
    this.#scene = document.createElementNS(SVG_NS, 'g');
    this.#overlay = document.createElementNS(SVG_NS, 'g');
    this.#overlay.classList.add('overlay');
    this.#backdrop = document.createElementNS(SVG_NS, 'g');
    this.#backdrop.classList.add('backdrop');
    this.#svg.append(this.#backdrop, this.#scene, this.#overlay);
    root.append(this.#svg);
    // The grid covers the whole canvas, which grows and shrinks with the window.
    new ResizeObserver(() => this.#drawBackdrop()).observe(this);
    this.#svg.addEventListener('pointerdown', (e) => this.#onPointerDown(e));
    this.#svg.addEventListener('pointermove', (e) => this.#onPointerMove(e));
    this.#svg.addEventListener('pointerup', (e) => this.#onPointerUp(e));
    this.#svg.addEventListener('dblclick', (e) => this.#startEditing(e));
    this.addEventListener('keydown', (e) => this.#onKeyDown(e));
  }

  /** Shows a newly rendered view; the selection keeps what still exists. */
  set view(view: RenderedView | undefined) {
    this.#view = view;
    if (!view) {
      this.#scene.innerHTML = '';
      this.#drawBackdrop();
      return;
    }
    const [, , width, height] = view.viewBox.split(' ').map(Number);
    this.#svg.setAttribute('viewBox', view.viewBox);
    this.#svg.setAttribute('width', String(width));
    this.#svg.setAttribute('height', String(height));
    this.#scene.innerHTML = view.content;
    this.#drawBackdrop();
    const elements = this.#selection.elements.filter((id) => view.nodes[id]);
    const relationships = this.#selection.relationships.filter((id) => view.edges[id]);
    if (
      elements.length !== this.#selection.elements.length ||
      relationships.length !== this.#selection.relationships.length
    )
      this.#select({ elements, relationships });
    else this.#drawOverlay();
  }

  get view(): RenderedView | undefined {
    return this.#view;
  }

  set selection(selection: Selection) {
    this.#selection = selection;
    this.#drawOverlay();
  }

  /** The grid's step in diagram units; 0 hides it and stops snapping. */
  get grid(): number {
    return this.#grid;
  }

  set grid(step: number) {
    this.#grid = Number.isFinite(step) && step > 0 ? step : 0;
    this.#drawBackdrop();
    this.#drawOverlay();
  }

  get selection(): Selection {
    return this.#selection;
  }

  // ── Events out ──

  #emit(type: string, detail?: unknown): void {
    this.dispatchEvent(new CustomEvent(type, { detail, bubbles: true, composed: true }));
  }

  #select(selection: Selection): void {
    this.#selection = selection;
    this.#drawOverlay();
    this.#emit('dd-select', { selection });
  }

  // ── Geometry helpers ──

  #toSvg(e: { clientX: number; clientY: number }): Point {
    const ctm = this.#svg.getScreenCTM();
    if (!ctm) return { x: 0, y: 0 };
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(ctm.inverse());
    return { x: p.x, y: p.y };
  }

  #toLocal(b: Box): Box {
    const ctm = this.#svg.getScreenCTM()!;
    const host = this.getBoundingClientRect();
    const tl = new DOMPoint(b.x, b.y).matrixTransform(ctm);
    const br = new DOMPoint(b.x + b.width, b.y + b.height).matrixTransform(ctm);
    return {
      x: tl.x - host.left + this.scrollLeft,
      y: tl.y - host.top + this.scrollTop,
      width: br.x - tl.x,
      height: br.y - tl.y,
    };
  }

  #leaves(): string[] {
    const view = this.#view;
    if (!view) return [];
    const containers = new Set(view.containers);
    return view.elements.filter((id) => !containers.has(id));
  }

  #nodeAt(p: Point): string | undefined {
    const view = this.#view;
    return this.#leaves().find((id) => {
      const b = view?.nodes[id];
      return b && insideTurned(b, this.#rotation(id), p);
    });
  }

  /** The smallest leaf under `p`, else the smallest container: where a line's end may go. */
  #elementAt(p: Point): string | undefined {
    const view = this.#view;
    if (!view) return undefined;
    const containers = new Set(view.containers);
    let best: { id: string; leaf: boolean; area: number } | undefined;
    for (const id of view.elements) {
      const b = view.nodes[id];
      if (!b || !insideTurned(b, this.#rotation(id), p)) continue;
      const leaf = !containers.has(id);
      const area = b.width * b.height;
      if (!best || (leaf && !best.leaf) || (leaf === best.leaf && area < best.area))
        best = { id, leaf, area };
    }
    return best?.id;
  }

  /** The relationship whose ends can be dragged: the only thing selected, and in the text. */
  #reconnectable(): string | undefined {
    const view = this.#view;
    const { elements, relationships } = this.#selection;
    if (!view || elements.length || relationships.length !== 1) return undefined;
    const id = relationships[0]!;
    return view.ends[id] && (view.edges[id]?.points.length ?? 0) >= 2 ? id : undefined;
  }

  /** Whether a move carries the selection along: the grabbed node is one of several selected. */
  #grouped(g: Gesture): boolean {
    const elements = this.#selection.elements;
    return g.kind === 'move' && elements.length > 1 && elements.includes(g.id);
  }

  /**
   * What a band from `a` to `b` selects: the nodes whose (turned) boxes and the relationships
   * written in the text whose whole lines lie inside it.
   */
  #enclosed(a: Point, b: Point): Selection {
    const view = this.#view;
    if (!view) return { elements: [], relationships: [] };
    const area = {
      x: Math.min(a.x, b.x),
      y: Math.min(a.y, b.y),
      width: Math.abs(b.x - a.x),
      height: Math.abs(b.y - a.y),
    };
    const elements = view.elements.filter((id) => {
      const box = view.nodes[id];
      if (!box) return false;
      const r = rotatedBounds(box, this.#rotation(id));
      return inside(area, r) && inside(area, { x: r.x + r.width, y: r.y + r.height });
    });
    const relationships = Object.keys(view.ends).filter((id) => {
      const points = view.edges[id]?.points;
      return !!points?.length && points.every((p) => inside(area, p));
    });
    return { elements, relationships };
  }

  /** The selection a band shows while dragged and makes when let go. */
  #banded(g: Extract<Gesture, { kind: 'band' }>): Selection {
    const add = this.#enclosed(g.start, g.at);
    return g.extend ? united(this.#selection, add) : add;
  }

  #moved(g: Extract<Gesture, { kind: 'move' }>): Box | undefined {
    const b = this.#view?.nodes[g.id];
    if (!b) return undefined;
    // A drop placed by its neighbours goes where the layout puts it, so only a plain one snaps.
    const x = b.x + g.delta.x;
    const y = b.y + g.delta.y;
    return g.freeform ? { ...b, x: snap(x, this.#grid), y: snap(y, this.#grid) } : { ...b, x, y };
  }

  /** A node's turn: degrees clockwise about its centre (0 when upright). */
  #rotation(id: string): number {
    return this.#view?.rotations?.[id] ?? 0;
  }

  /** Diagram units per screen pixel. */
  #unitsPerPixel(): number {
    const a = this.#svg.getScreenCTM()?.a;
    return a ? 1 / a : 1;
  }

  /** The one selected node, when it can be resized: any in a fixed view, elsewhere leaves only. */
  #resizable(): string | undefined {
    const view = this.#view;
    const sel = this.#selection;
    if (!view || sel.elements.length !== 1 || sel.relationships.length) return undefined;
    const id = sel.elements[0]!;
    if (!view.nodes[id]) return undefined;
    // A layout sizes containers around their children; only a fixed view takes theirs as written.
    return view.algorithm === 'fixed' || !view.containers.includes(id) ? id : undefined;
  }

  /** The one selected leaf node, when it can be turned (not in sequence views). */
  #rotatable(): string | undefined {
    const view = this.#view;
    const sel = this.#selection;
    if (
      !view ||
      view.algorithm === 'sequence' ||
      sel.elements.length !== 1 ||
      sel.relationships.length
    )
      return undefined;
    const id = sel.elements[0]!;
    return view.nodes[id] && !view.containers.includes(id) ? id : undefined;
  }

  #rotateHandle(): { id: string; at: Point } | undefined {
    const id = this.#rotatable();
    if (id === undefined) return undefined;
    const reach = ROTATE_REACH * Math.max(1, this.#unitsPerPixel());
    return { id, at: rotateHandle(this.#view!.nodes[id]!, this.#rotation(id), reach) };
  }

  #bendable(): string | undefined {
    const sel = this.#selection;
    if (this.#view?.algorithm !== 'fixed' || sel.relationships.length !== 1 || sel.elements.length)
      return undefined;
    return (this.#view.edges[sel.relationships[0]!]?.points.length ?? 0) >= 2
      ? sel.relationships[0]
      : undefined;
  }

  #resized(g: Extract<Gesture, { kind: 'resize' }>): Box | undefined {
    const b = this.#view?.nodes[g.id];
    if (!b || !g.moved) return undefined;
    return resizedBox(b, this.#rotation(g.id), g.handle, g.start, g.at, g.keep, {
      step: this.#grid,
      fixed: this.#view!.algorithm === 'fixed',
    });
  }

  #turned(g: Extract<Gesture, { kind: 'rotate' }>): number | undefined {
    const b = this.#view?.nodes[g.id];
    if (!b || !g.moved) return undefined;
    return turned(b, this.#rotation(g.id), g.start, g.at);
  }

  /** The route with the held bend point where the pointer is (or gone, if it lies straight). */
  #bent(g: Extract<Gesture, { kind: 'bend' }>): Point[] | undefined {
    const route = this.#view?.edges[g.id];
    if (!route || !g.moved) return undefined;
    const points = [...route.points];
    const at = { x: snap(g.at.x, this.#grid), y: snap(g.at.y, this.#grid) };
    const i = g.insert ? g.handle + 1 : g.handle;
    if (g.insert) points.splice(i, 0, at);
    else if (i > 0 && i < points.length - 1) points[i] = at;
    else return undefined;
    if (distanceToSegment(at, points[i - 1]!, points[i + 1]!) <= STRAIGHTEN) points.splice(i, 1);
    return points;
  }

  static #hit(target: EventTarget | null, cls: 'dd-node' | 'dd-edge'): string | undefined {
    return (target as Element | null)?.closest?.(`.${cls}`)?.getAttribute('data-id') ?? undefined;
  }

  // ── Pointer gestures ──

  #onPointerDown(e: PointerEvent): void {
    if (this.#editor) return;
    this.focus();
    const p = this.#toSvg(e);
    const target = e.target as Element;
    const grip = Number(target.getAttribute('data-handle'));
    const resizing = target.classList.contains('dd-resize') && this.#resizable();
    const bending = target.classList.contains('dd-bend') && this.#bendable();
    if (resizing || bending) {
      this.#gesture = resizing
        ? { kind: 'resize', id: resizing, handle: grip, start: p, at: p, moved: false, keep: false }
        : {
            kind: 'bend',
            id: bending as string,
            handle: grip,
            insert: target.classList.contains('add'),
            start: p,
            at: p,
            moved: false,
          };
      target.setPointerCapture?.(e.pointerId);
      return;
    }
    const rotating = target.classList.contains('dd-rotate') && this.#rotatable();
    if (rotating) {
      this.#gesture = { kind: 'rotate', id: rotating, start: p, at: p, moved: false };
      target.setPointerCapture?.(e.pointerId);
      return;
    }
    if (target.classList.contains('dd-handle')) {
      this.#gesture = { kind: 'connect', from: target.getAttribute('data-for')!, to: p };
      target.setPointerCapture?.(e.pointerId);
      this.#drawOverlay();
      return;
    }
    if (target.classList.contains('dd-end')) {
      this.#gesture = {
        kind: 'reconnect',
        id: target.getAttribute('data-for')!,
        end: target.getAttribute('data-end') as End,
        to: p,
      };
      target.setPointerCapture?.(e.pointerId);
      this.#drawOverlay();
      return;
    }
    const node = DiagramCanvas.#hit(target, 'dd-node');
    const edge = DiagramCanvas.#hit(target, 'dd-edge');
    const sel = this.#selection;
    // Shift, or ⌘ on macOS and Ctrl elsewhere, adds an item to the selection or takes it out.
    const extend = e.shiftKey || e.metaKey || e.ctrlKey;
    if (node) {
      const has = sel.elements.includes(node);
      let now = sel;
      if (extend) {
        now = {
          ...sel,
          elements: has ? sel.elements.filter((x) => x !== node) : [...sel.elements, node],
        };
        this.#select(now);
      } else if (!has) {
        now = { elements: [node], relationships: [] };
        this.#select(now);
      }
      const several = now.elements.length > 1 || now.relationships.length > 0;
      this.#gesture = { kind: 'press', id: node, start: p, narrow: has && several && !extend };
    } else if (edge) {
      const has = sel.relationships.includes(edge);
      this.#select(
        extend
          ? {
              ...sel,
              relationships: has
                ? sel.relationships.filter((x) => x !== edge)
                : [...sel.relationships, edge],
            }
          : { elements: [], relationships: [edge] },
      );
    } else {
      // Empty canvas: a click clears the selection, a drag draws a band.
      if (!extend) this.#select({ elements: [], relationships: [] });
      this.#gesture = { kind: 'band', start: p, at: p, moved: false, extend };
    }
  }

  #onPointerMove(e: PointerEvent): void {
    const p = this.#toSvg(e);
    const g = this.#gesture;
    if (g.kind === 'press' || g.kind === 'move') {
      const delta = { x: p.x - g.start.x, y: p.y - g.start.y };
      if (g.kind === 'press') {
        if (Math.hypot(delta.x, delta.y) < DRAG_THRESHOLD) return;
        // Capture only once a drag starts, so clicks and double-clicks keep their real target.
        this.#svg.setPointerCapture?.(e.pointerId);
      }
      const next: Gesture = { kind: 'move', id: g.id, start: g.start, delta, freeform: true };
      // A plain drop stays where it is let go; Alt asks for a placement by the neighbours. A group
      // always lands where it is let go: its nodes keep their places relative to each other.
      next.freeform = !e.altKey || this.#grouped(next);
      this.#gesture = next;
      this.#drawOverlay();
    } else if (g.kind === 'band') {
      if (!g.moved) {
        if (Math.hypot(p.x - g.start.x, p.y - g.start.y) < DRAG_THRESHOLD) return;
        this.#svg.setPointerCapture?.(e.pointerId);
      }
      this.#gesture = { ...g, at: p, moved: true };
      this.#drawOverlay();
    } else if (g.kind === 'resize' || g.kind === 'bend' || g.kind === 'rotate') {
      if (!g.moved && Math.hypot(p.x - g.start.x, p.y - g.start.y) < DRAG_THRESHOLD) return;
      // A resize keeps the proportions its preview last showed, even if Shift is let go first.
      const keep = g.kind === 'resize' && (e.type === 'pointerup' && g.moved ? g.keep : e.shiftKey);
      this.#gesture =
        g.kind === 'resize' ? { ...g, at: p, moved: true, keep } : { ...g, at: p, moved: true };
      this.#drawOverlay();
    } else if (g.kind === 'connect') {
      const target = this.#nodeAt(p);
      this.#gesture = {
        kind: 'connect',
        from: g.from,
        to: p,
        ...(target !== undefined && target !== g.from ? { target } : {}),
      };
      this.#drawOverlay();
    } else if (g.kind === 'reconnect') {
      // Not onto the element either end is already on.
      const ends = this.#view?.ends[g.id];
      const target = this.#elementAt(p);
      const free = target !== undefined && target !== ends?.source && target !== ends?.target;
      this.#gesture = {
        kind: 'reconnect',
        id: g.id,
        end: g.end,
        to: p,
        ...(free ? { target } : {}),
      };
      this.#drawOverlay();
    }
  }

  #onPointerUp(e: PointerEvent): void {
    this.#onPointerMove(e);
    const g = this.#gesture;
    this.#gesture = { kind: 'idle' };
    if (g.kind === 'move' && this.#grouped(g)) {
      const box = this.#moved(g);
      const from = this.#view?.nodes[g.id];
      // A copy: applying the move renders the view again, which can change the selection.
      const ids = [...this.#selection.elements];
      if (box && from && (box.x !== from.x || box.y !== from.y))
        this.#emit('dd-move', { ids, offset: { x: box.x - from.x, y: box.y - from.y } });
    } else if (g.kind === 'move') {
      const box = this.#moved(g);
      if (box) this.#emit('dd-drop', { id: g.id, box, freeform: g.freeform });
    } else if (g.kind === 'press' && g.narrow) {
      this.#select({ elements: [g.id], relationships: [] });
    } else if (g.kind === 'band' && g.moved) {
      this.#select(this.#banded(g));
    } else if (g.kind === 'connect' && g.target !== undefined) {
      this.#emit('dd-connect', { source: g.from, target: g.target });
    } else if (g.kind === 'resize') {
      const box = this.#resized(g);
      if (box) this.#emit('dd-resize', { id: g.id, box });
      // The overlay is drawn again below, which replaces the corner under the pointer, so the
      // browser sends no click or dblclick for it: a double-click is told apart here instead.
      const tap = this.#gripTap;
      if (g.moved) {
        this.#gripTap = undefined;
      } else if (tap && tap.id === g.id && e.timeStamp - tap.time < DOUBLE_TAP_MS) {
        this.#gripTap = undefined;
        this.#emit('dd-fit', { id: g.id });
      } else {
        this.#gripTap = { id: g.id, time: e.timeStamp };
      }
    } else if (g.kind === 'rotate') {
      const degrees = this.#turned(g);
      if (degrees !== undefined && degrees !== this.#rotation(g.id))
        this.#emit('dd-rotate', { id: g.id, degrees });
    } else if (g.kind === 'bend') {
      const route = this.#bent(g);
      if (route) this.#emit('dd-reroute', { id: g.id, points: route.slice(1, -1) });
    } else if (g.kind === 'reconnect' && g.target !== undefined) {
      this.#emit('dd-reconnect', { id: g.id, end: g.end, element: g.target });
    }
    this.#drawOverlay();
  }

  // ── Inline rename ──

  #startEditing(e: MouseEvent): void {
    const view = this.#view;
    if (!view) return;
    // A double-click on a resize corner fits the node (told apart in #onPointerUp), never renames.
    if (e.target instanceof Element && e.target.classList.contains('dd-resize')) return;
    const node = DiagramCanvas.#hit(e.target, 'dd-node');
    const edge = DiagramCanvas.#hit(e.target, 'dd-edge');
    let at: Box | undefined;
    let value = '';
    let commit: (v: string) => void;
    let multiline = false;
    if (node && view.nodes[node]) {
      multiline = true;
      at = this.#toLocal(view.nodes[node]);
      value = this.nameOf(node);
      commit = (v) => {
        if (v.trim() && v !== value) this.#emit('dd-rename', { id: node, name: v.trim() });
      };
    } else if (edge && view.edges[edge]) {
      const route = view.edges[edge];
      const anchor = route.label ?? labelAnchor(route.points);
      if (!anchor) return;
      at = this.#toLocal(anchor);
      value = this.labelOf(edge);
      commit = (v) => {
        if (v.trim() !== value)
          this.#emit('dd-relabel', { id: edge, label: v.trim() || undefined });
      };
    } else {
      return;
    }
    // A name may span lines (it is drawn with its line breaks): Enter starts a new one and
    // Ctrl/⌘+Enter commits. A relationship's label is one line, committed by Enter.
    const input = multiline
      ? Object.assign(document.createElement('textarea'), {
          className: 'inline-edit',
          value,
          rows: 1,
        })
      : Object.assign(document.createElement('input'), { className: 'inline-edit', value });
    input.setAttribute('aria-label', node ? 'Element name' : 'Relationship label');
    Object.assign(input.style, {
      left: `${at.x}px`,
      top: `${at.y + at.height / 2 - 14}px`,
      width: `${Math.max(120, at.width)}px`,
    });
    // The field grows with its lines, down from where a one-line field would sit.
    const fit = () => {
      if (!(input instanceof HTMLTextAreaElement)) return;
      input.style.height = 'auto';
      input.style.height = `${input.scrollHeight + 4}px`;
    };
    let done = false;
    const finish = (save: boolean) => {
      if (done) return;
      done = true;
      if (save) commit(input.value);
      input.remove();
      this.#editor = undefined;
      this.focus();
    };
    input.onkeydown = (ev) => {
      ev.stopPropagation();
      if (ev.key === 'Enter' && !ev.isComposing && (!multiline || ev.ctrlKey || ev.metaKey)) {
        ev.preventDefault();
        finish(true);
      }
      if (ev.key === 'Escape') finish(false);
    };
    input.addEventListener('input', fit);
    input.addEventListener('blur', () => finish(true));
    this.shadowRoot!.append(input);
    this.#editor = input;
    fit();
    input.focus();
    input.select();
  }

  // ── Keys ──

  #onKeyDown(e: KeyboardEvent): void {
    if (this.#editor) return;
    const mod = e.metaKey || e.ctrlKey;
    if (mod && e.key.toLowerCase() === 'z') {
      e.preventDefault();
      this.#emit(e.shiftKey ? 'dd-redo' : 'dd-undo');
    } else if (mod && e.key.toLowerCase() === 'a') {
      e.preventDefault();
      this.#selectAll();
    } else if (mod && e.key.toLowerCase() === 'y') {
      e.preventDefault();
      this.#emit('dd-redo');
    } else if (e.key === 'Delete' || e.key === 'Backspace') {
      const sel = this.#selection;
      if (sel.elements.length || sel.relationships.length) {
        e.preventDefault();
        this.#emit('dd-delete', { selection: sel });
        this.#select({ elements: [], relationships: [] });
      }
    } else if (e.key === 'F2') {
      // Rename Element: the app asks for the selected element's new name.
      e.preventDefault();
      this.#emit('dd-rename-element');
    } else if (e.key === 'Escape') {
      this.#gesture = { kind: 'idle' };
      this.#select({ elements: [], relationships: [] });
    }
  }

  // ── Backdrop ──

  /**
   * With the grid shown: the drawing's background spread over the whole canvas, and the grid on
   * it, both under the drawing (whose own background is hidden).
   */
  #drawBackdrop(): void {
    const view = this.#view;
    const ctm = this.#svg.getScreenCTM();
    const shown = this.#grid > 0 && !!view && !!ctm;
    this.#svg.classList.toggle('gridded', shown);
    if (!shown) {
      this.#backdrop.innerHTML = '';
      return;
    }
    // The canvas's whole scrollable area, in diagram units.
    const host = this.getBoundingClientRect();
    const left = host.left + this.clientLeft - this.scrollLeft;
    const top = host.top + this.clientTop - this.scrollTop;
    const inverse = ctm.inverse();
    const from = new DOMPoint(left, top).matrixTransform(inverse);
    const to = new DOMPoint(
      left + Math.max(this.scrollWidth, this.clientWidth),
      top + Math.max(this.scrollHeight, this.clientHeight),
    ).matrixTransform(inverse);
    const background = this.#scene.querySelector('.dd-background')?.getAttribute('fill');
    const px = this.#unitsPerPixel();
    const lines = gridLines(from, to, this.#grid, px);
    const path = (ls: [number, boolean][]) =>
      ls
        .map(([v, vertical]) => (vertical ? `M${v} ${from.y}V${to.y}` : `M${from.x} ${v}H${to.x}`))
        .join('');
    // On a dark background the lines are light.
    const dark = background !== undefined && background !== null && isDark(background);
    const ink = dark ? '#ffffff' : '#64748b';
    const fade = dark ? 0.6 : 1;
    const parts: string[] = [];
    if (background)
      parts.push(
        `<rect x="${from.x}" y="${from.y}" width="${to.x - from.x}" height="${to.y - from.y}" fill="${escape(background)}"/>`,
      );
    if (lines.minor.length)
      parts.push(
        `<path d="${path(lines.minor)}" stroke="${ink}" stroke-width="${px}" stroke-opacity="${0.14 * fade}"/>`,
      );
    if (lines.major.length)
      parts.push(
        `<path d="${path(lines.major)}" stroke="${ink}" stroke-width="${px}" stroke-opacity="${0.28 * fade}"/>`,
      );
    this.#backdrop.innerHTML = parts.join('');
  }

  /** Selects every node and every relationship drawn in the view (implied lines excepted). */
  #selectAll(): void {
    const view = this.#view;
    if (!view) return;
    this.#gesture = { kind: 'idle' };
    this.#select({
      elements: view.elements.filter((id) => view.nodes[id]),
      relationships: Object.keys(view.ends).filter((id) => view.edges[id]),
    });
  }

  // ── Overlay ──

  #drawOverlay(): void {
    const view = this.#view;
    const parts: string[] = [];
    if (view) {
      const g = this.#gesture;
      // What is selected, and what a band being dragged would select.
      const band = g.kind === 'band' && g.moved ? g : undefined;
      const shown = band ? this.#banded(band) : this.#selection;
      for (const id of shown.relationships) {
        const route = view.edges[id];
        if (route)
          parts.push(
            `<polyline class="sel-edge" points="${route.points.map((p) => `${p.x},${p.y}`).join(' ')}"/>`,
          );
      }
      const turning = g.kind === 'rotate' ? this.#turned(g) : undefined;
      for (const id of shown.elements) {
        const b = view.nodes[id];
        // While it turns, the outline turns with the pointer.
        const degrees =
          turning !== undefined && g.kind === 'rotate' && g.id === id
            ? turning
            : this.#rotation(id);
        if (b)
          parts.push(
            `<rect class="sel-node" x="${b.x - 4}" y="${b.y - 4}" width="${b.width + 8}" height="${b.height + 8}" rx="8"${turn(b, degrees)}/>`,
          );
      }
      if (band) {
        const x = Math.min(band.start.x, band.at.x);
        const y = Math.min(band.start.y, band.at.y);
        parts.push(
          `<rect class="band" x="${x}" y="${y}" width="${Math.abs(band.at.x - band.start.x)}" height="${Math.abs(band.at.y - band.start.y)}"/>`,
        );
      }
      if (g.kind === 'move' && this.#grouped(g)) {
        // The whole group, each node offset as the grabbed one is.
        const box = this.#moved(g);
        const from = view.nodes[g.id];
        if (box && from) {
          const dx = box.x - from.x;
          const dy = box.y - from.y;
          let drawn = 0;
          for (const id of this.#selection.elements) {
            const b = view.nodes[id];
            if (!b) continue;
            parts.push(ghost({ ...b, x: b.x + dx, y: b.y + dy }, 'ghost', this.#rotation(id)));
            drawn++;
          }
          parts.push(
            `<text class="hint-label" x="${box.x}" y="${box.y - 10}">${drawn} elements</text>`,
          );
        }
      } else if (g.kind === 'move') {
        const box = this.#moved(g);
        if (box) {
          parts.push(ghost(box, 'ghost', this.#rotation(g.id)));
          const hint = this.previewDrop(g.id, box, g.freeform);
          if (hint)
            parts.push(
              `<text class="hint-label" x="${box.x}" y="${box.y - 10}">${escape(describeHint(hint, this.nameOf))}</text>`,
            );
        }
      } else if (g.kind === 'connect') {
        const from = view.nodes[g.from];
        if (from) {
          const start = rotatePoint(
            { x: from.x + from.width, y: from.y + from.height / 2 },
            center(from),
            this.#rotation(g.from),
          );
          parts.push(
            `<line class="connect-line" x1="${start.x}" y1="${start.y}" x2="${g.to.x}" y2="${g.to.y}"/>`,
          );
          const target = g.target === undefined ? undefined : view.nodes[g.target];
          if (target) parts.push(ghost(target, 'connect-target'));
        }
      } else if (g.kind === 'reconnect') {
        // The line as it would be: from the end that stays to the pointer.
        const points = view.edges[g.id]?.points;
        const fixed = points && (g.end === 'source' ? points[points.length - 1] : points[0]);
        if (fixed)
          parts.push(
            `<line class="connect-line" x1="${fixed.x}" y1="${fixed.y}" x2="${g.to.x}" y2="${g.to.y}"/>`,
          );
        const target = g.target === undefined ? undefined : view.nodes[g.target];
        if (target) parts.push(ghost(target, 'connect-target'));
      }
      const resized = g.kind === 'resize' ? this.#resized(g) : undefined;
      if (resized && g.kind === 'resize')
        parts.push(
          ghost(resized, 'ghost', this.#rotation(g.id)),
          `<text class="hint-label" x="${resized.x}" y="${resized.y - 10}">size ${Math.round(resized.width)} × ${Math.round(resized.height)}</text>`,
        );
      const turnedBox = g.kind === 'rotate' && turning !== undefined ? view.nodes[g.id] : undefined;
      if (turnedBox) {
        const bounds = rotatedBounds(turnedBox, turning!);
        parts.push(
          ghost(turnedBox, 'ghost', turning!),
          `<text class="hint-label" x="${bounds.x}" y="${bounds.y - 10}">${turning}°</text>`,
        );
      }
      const bent = g.kind === 'bend' ? this.#bent(g) : undefined;
      if (bent)
        parts.push(
          `<polyline class="bent" points="${bent.map((p) => `${p.x},${p.y}`).join(' ')}"/>`,
        );
      const busy =
        g.kind === 'move' ||
        g.kind === 'resize' ||
        g.kind === 'bend' ||
        g.kind === 'rotate' ||
        !!band;
      const handleFor =
        this.#selection.elements.length === 1 ? this.#selection.elements[0] : undefined;
      const hb = handleFor === undefined ? undefined : view.nodes[handleFor];
      // The rotate handle goes first, so a resize corner is on top where the two meet.
      const rotate = busy ? undefined : this.#rotateHandle();
      if (rotate) {
        const b = view.nodes[rotate.id]!;
        const top = rotatePoint({ x: b.x + b.width, y: b.y }, center(b), this.#rotation(rotate.id));
        parts.push(
          `<line class="stalk" x1="${top.x}" y1="${top.y}" x2="${rotate.at.x}" y2="${rotate.at.y}"/>`,
          `<circle class="dd-rotate" cx="${rotate.at.x}" cy="${rotate.at.y}" r="${GRIP}"><title>Drag to turn</title></circle>`,
        );
      }
      const resizable = this.#resizable();
      if (resizable !== undefined && !busy) {
        const b = view.nodes[resizable]!;
        const degrees = this.#rotation(resizable);
        corners(b).forEach((corner, i) => {
          const c = rotatePoint(corner, center(b), degrees);
          const spin = degrees ? ` transform="rotate(${degrees} ${c.x} ${c.y})"` : '';
          parts.push(
            `<rect class="dd-resize" data-handle="${i}" x="${c.x - GRIP}" y="${c.y - GRIP}" width="${2 * GRIP}" height="${2 * GRIP}" rx="1"${spin}><title>Drag to resize, double-click to fit the content</title></rect>`,
          );
        });
      }
      const bendable = this.#bendable();
      if (bendable !== undefined && !busy) {
        const points = view.edges[bendable]!.points;
        points
          .slice(1, -1)
          .forEach((p, i) =>
            parts.push(
              `<circle class="dd-bend" data-handle="${i + 1}" cx="${p.x}" cy="${p.y}" r="${GRIP}"><title>Drag to move this bend</title></circle>`,
            ),
          );
        points
          .slice(1)
          .forEach((p, i) =>
            parts.push(
              `<circle class="dd-bend add" data-handle="${i}" cx="${(points[i]!.x + p.x) / 2}" cy="${(points[i]!.y + p.y) / 2}" r="${GRIP * 0.7}"><title>Drag to add a bend</title></circle>`,
            ),
          );
      }
      if (hb && !busy) {
        // Off the middle of the right edge, turned with the node.
        const c = rotatePoint(
          { x: hb.x + hb.width + 10, y: hb.y + hb.height / 2 },
          center(hb),
          this.#rotation(handleFor!),
        );
        parts.push(
          `<circle class="dd-handle" data-for="${escape(handleFor!)}" cx="${c.x}" cy="${c.y}" r="7"><title>Drag to another element to connect</title></circle>`,
        );
      }
      const line = busy ? undefined : this.#reconnectable();
      const ends = line === undefined ? undefined : view.edges[line]?.points;
      if (line !== undefined && ends) {
        const at: [End, Point][] = [
          ['source', ends[0]!],
          ['target', ends[ends.length - 1]!],
        ];
        for (const [end, p] of at)
          if (!(g.kind === 'reconnect' && g.end === end))
            parts.push(
              `<circle class="dd-end" data-for="${escape(line)}" data-end="${end}" cx="${p.x}" cy="${p.y}" r="6"><title>Drag to another element to reconnect</title></circle>`,
            );
      }
      this.#svg.classList.toggle('dragging', busy);
    }
    this.#overlay.innerHTML = parts.join('');
  }
}

/** Whether a CSS colour is dark (a translucent one is seen over the near-white canvas). */
function isDark(color: string): boolean {
  const ctx = document.createElement('canvas').getContext('2d');
  if (!ctx) return false;
  ctx.fillStyle = '#ffffff';
  ctx.fillStyle = color;
  const c = ctx.fillStyle; // '#rrggbb', or 'rgba(r, g, b, a)' when translucent
  let r: number, g: number, b: number;
  let a = 1;
  if (c.startsWith('#')) {
    [r, g, b] = [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16) / 255) as [
      number,
      number,
      number,
    ];
  } else {
    const n = c.match(/[\d.]+/g)?.map(Number) ?? [];
    [r, g, b] = [(n[0] ?? 255) / 255, (n[1] ?? 255) / 255, (n[2] ?? 255) / 255];
    a = n[3] ?? 1;
  }
  return (0.2126 * r + 0.7152 * g + 0.0722 * b) * a + (1 - a) < 0.5;
}

/** `base` with the ids of `add` it lacks appended after its own. */
function united(base: Selection, add: Selection): Selection {
  const more = (have: readonly string[], extra: readonly string[]) => [
    ...have,
    ...extra.filter((id) => !have.includes(id)),
  ];
  return {
    elements: more(base.elements, add.elements),
    relationships: more(base.relationships, add.relationships),
  };
}

function ghost(b: Box, cls: string, degrees = 0): string {
  return `<rect class="${cls}" x="${b.x}" y="${b.y}" width="${b.width}" height="${b.height}" rx="6"${turn(b, degrees)}/>`;
}

/** A `transform` attribute turning `b` by `degrees` about its centre (none when upright). */
function turn(b: Box, degrees: number): string {
  if (!degrees) return '';
  const c = center(b);
  return ` transform="rotate(${degrees} ${c.x} ${c.y})"`;
}

function escape(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function labelAnchor(points: readonly Point[]): Box | undefined {
  if (points.length < 2) return undefined;
  const i = Math.floor((points.length - 1) / 2);
  const a = points[i]!;
  const b = points[i + 1]!;
  return { x: (a.x + b.x) / 2 - 40, y: (a.y + b.y) / 2 - 10, width: 80, height: 20 };
}

/** A short human phrase for a hint, shown while dragging. */
export function describeHint(hint: LayoutHint, name: (id: string) => string): string {
  switch (hint.kind) {
    case 'pin':
      return `${hint.placement.replace('-', ' ')} ${name(hint.reference)}`;
    case 'pinAt':
      return `at ${hint.x}, ${hint.y}`;
    case 'align':
      return `align ${hint.axis === 'horizontal' ? 'with row of' : 'with column of'} ${name(hint.elements[0]!)}`;
    case 'order':
      return `order ${hint.elements.map(name).join(', ')}`;
    case 'route':
      return `route ${hint.relationship}`;
    case 'size':
      return `size ${hint.width} × ${hint.height}`;
    case 'rotate':
      return `rotate ${hint.degrees}°`;
  }
}

customElements.define('dd-canvas', DiagramCanvas);

declare global {
  interface HTMLElementTagNameMap {
    'dd-canvas': DiagramCanvas;
  }
}
