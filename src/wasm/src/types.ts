/**
 * The values that cross the WebAssembly boundary: the document model, operations and layout
 * geometry, in the JSON shapes the C++ core reads and writes (src/model/src/json.cpp).
 */

export type ElementId = string;
export type RelationshipId = string;
export type GroupId = string;
export type ViewId = string;

/** Free-form key/value properties attached to model items. */
export type Properties = Readonly<Record<string, string>>;

/**
 * Visual style, as `key: value` pairs. Known keys (see docs/dsl.md): `fill`, `stroke`,
 * `strokeWidth`, `dashed`, `rounded`, `opacity`, `fontColor`, `fontSize`, `bold`, `italic`,
 * `align`, `verticalAlign` for elements; `stroke`, `strokeWidth`, `dashed`, `startArrow`,
 * `endArrow`, `edgeStyle`, `fontColor`, `fontSize` for relationships. Unknown keys are kept.
 */
export type Style = Readonly<Record<string, string>>;

export interface Point2 {
  readonly x: number;
  readonly y: number;
}

export type PortSide = 'north' | 'south' | 'east' | 'west';

/** A named connection point on an element. Relationships may attach to a port. */
export interface Port {
  readonly id: string;
  readonly side?: PortSide;
}

/**
 * Element kinds are open-ended so diagram-type plugins can add their own. These are the kinds the
 * first milestone renders (flowchart and C4/component).
 */
export type BuiltinElementKind =
  | 'person'
  | 'system'
  | 'container'
  | 'component'
  | 'database'
  | 'process'
  | 'decision'
  | 'start'
  | 'end'
  | 'note'
  | 'class'
  | 'object'
  // BPMN, drawn with the shapes of the built-in `bpmn` library.
  | 'event'
  | 'task'
  | 'subprocess'
  | 'gateway'
  | 'data'
  | 'annotation'
  | 'group'
  | 'pool'
  | 'lane'
  | 'conversation';

export type ElementKind = BuiltinElementKind | (string & {});

export interface Element {
  readonly id: ElementId;
  readonly kind: ElementKind;
  /** Display name; defaults to the id in the DSL when no label is given. */
  readonly name: string;
  /** Containing element, for nested elements such as containers inside a system. */
  readonly parent?: ElementId;
  /** Visual group the element belongs to (a boundary that is not itself an element). */
  readonly group?: GroupId;
  readonly technology?: string;
  readonly description?: string;
  readonly shape?: string;
  readonly tags: readonly string[];
  readonly ports: readonly Port[];
  readonly properties: Properties;
  readonly style?: Style;
}

export type LineStyle = 'solid' | 'dashed';

export interface Endpoint {
  readonly element: ElementId;
  readonly port?: string;
}

export interface Relationship {
  readonly id: RelationshipId;
  readonly source: Endpoint;
  readonly target: Endpoint;
  readonly label?: string;
  readonly technology?: string;
  readonly line: LineStyle;
  readonly bidirectional: boolean;
  readonly tags: readonly string[];
  readonly properties: Properties;
  readonly style?: Style;
  /** Set on relationships a view lifted from hidden descendants; never stored in a model. */
  readonly implied?: boolean;
}

/** A named boundary around elements that is not itself part of the model's hierarchy. */
export interface Group {
  readonly id: GroupId;
  readonly name: string;
  readonly parent?: GroupId;
}

export type Direction = 'right' | 'left' | 'down' | 'up';

export interface LayoutOptions {
  /** Layout algorithm, e.g. `layered`. Engines map names they know and reject the rest. */
  readonly algorithm: string;
  readonly direction: Direction;
  /** Minimum spacing between nodes, in px. */
  readonly spacing?: number;
}

export type Placement = 'below' | 'above' | 'left-of' | 'right-of';

/**
 * Layout hints record user intent from canvas drags. Layout respects them; they are stored in the
 * view (and written to text) instead of absolute coordinates.
 */
export type LayoutHint =
  | {
      readonly kind: 'pin';
      readonly element: ElementId;
      readonly placement: Placement;
      readonly reference: ElementId;
    }
  | {
      readonly kind: 'pinAt';
      readonly element: ElementId;
      readonly x: number;
      readonly y: number;
      /** Fixed size, for freeform diagrams (imported drawings keep their shapes' sizes). */
      readonly width?: number;
      readonly height?: number;
    }
  | {
      /** A fixed route for a relationship: bend points, and where it leaves and enters. */
      readonly kind: 'route';
      readonly relationship: RelationshipId;
      readonly points: readonly Point2[];
      /** Exit point on the source, relative to its box (0..1 each way). */
      readonly exit?: Point2;
      /** Entry point on the target, relative to its box. */
      readonly entry?: Point2;
    }
  | {
      readonly kind: 'align';
      readonly elements: readonly ElementId[];
      readonly axis: 'horizontal' | 'vertical';
    }
  | { readonly kind: 'order'; readonly elements: readonly ElementId[] }
  /** `size a W, H`: the element's box in this view, which every layout keeps. */
  | {
      readonly kind: 'size';
      readonly element: ElementId;
      readonly width: number;
      readonly height: number;
    }
  /** `rotate a D`: the element turned D degrees clockwise about its centre. */
  | { readonly kind: 'rotate'; readonly element: ElementId; readonly degrees: number };

export type MessageStyle = 'sync' | 'async' | 'reply';
export type FrameKind = 'alt' | 'opt' | 'loop' | 'par' | 'critical' | 'break' | 'group';

/** One step of a sequence view, in order from top to bottom. */
export type SequenceItem =
  | {
      readonly kind: 'message';
      readonly from: ElementId;
      readonly to: ElementId;
      readonly label?: string;
      readonly style: MessageStyle;
    }
  | { readonly kind: 'activate' | 'deactivate'; readonly element: ElementId }
  | {
      readonly kind: 'note';
      readonly position: 'over' | 'left' | 'right';
      readonly over: readonly ElementId[];
      readonly text: string;
    }
  | { readonly kind: 'divider'; readonly label?: string }
  | { readonly kind: 'delay'; readonly label?: string }
  | { readonly kind: 'ref'; readonly over: readonly ElementId[]; readonly label: string }
  | {
      readonly kind: 'frame';
      readonly frame: FrameKind;
      readonly sections: readonly {
        readonly label?: string;
        readonly items: readonly SequenceItem[];
      }[];
    };

/**
 * A note, free text or box drawn on one view only (`note "…" right of web`, `text "…" at 0, -40`,
 * `box "PCI" around api db`). Placed after layout; not part of the model.
 */
export interface Decoration {
  readonly kind: 'note' | 'text' | 'box';
  /** Optional name, unique in the view. */
  readonly name?: string;
  readonly text?: string;
  /** Beside an element of the view. */
  readonly placement?: Placement;
  readonly reference?: ElementId;
  /** A box around these elements. */
  readonly around?: readonly ElementId[];
  /** Absolute position (and size). */
  readonly at?: Point2;
  readonly width?: number;
  readonly height?: number;
  readonly style?: Style;
}

/** `*` selects every element in the view's scope; otherwise an element id. */
export type Selector = '*' | ElementId;

export interface View {
  readonly id: ViewId;
  /** Diagram type, e.g. `flowchart` or `component`. Picks shapes and default layout. */
  readonly type?: string;
  /** Element whose children the view shows. Omitted for a view of the whole model. */
  readonly scope?: ElementId;
  readonly include: readonly Selector[];
  readonly exclude: readonly Selector[];
  readonly layout: LayoutOptions;
  readonly hints: readonly LayoutHint[];
  /** Steps of a sequence view (`layout sequence`); participants are the included elements. */
  readonly sequence?: readonly SequenceItem[];
  /** Number the messages of a sequence view. */
  readonly autonumber?: boolean;
  /** `theme dark` in the view: overrides the document's theme. */
  readonly theme?: string;
  /** Notes, text and boxes of this view, in source order; omitted when there are none. */
  readonly decorations?: readonly Decoration[];
}

/**
 * A `style` block: a look shared by every element (`style *`), every element of a kind
 * (`style container`), every item with a tag (`style tag "external"`) or every relationship
 * (`style relationship`). Inline `style "…"` on an item overrides it.
 */
export interface StyleRule {
  readonly target: 'elements' | 'relationships';
  /** Element kind; omitted for `*` and for relationships. */
  readonly kind?: string;
  /** Only items with this tag. */
  readonly tag?: string;
  readonly style: Style;
}

/**
 * A shape library a document uses: `use "aws-icons@^1" as aws`. Shapes are then referenced as
 * `shape aws.lambda`. Resolving the library is up to the host; the model only records the use.
 */
export interface LibraryUse {
  readonly name: string;
  /** A semver range, `*` when the spec has none. */
  readonly range: string;
  readonly alias: string;
}

export interface Model {
  readonly elements: Readonly<Record<ElementId, Element>>;
  readonly relationships: Readonly<Record<RelationshipId, Relationship>>;
  readonly groups: Readonly<Record<GroupId, Group>>;
  readonly views: Readonly<Record<ViewId, View>>;
  /** Libraries in `use` order; absent when the document uses none. */
  readonly libraries?: readonly LibraryUse[];
  /** `theme dark`: the built-in theme views are drawn with; the default theme when absent. */
  readonly theme?: string;
  /** `style` blocks in document order; absent when there are none. */
  readonly styles?: readonly StyleRule[];
  /** Themes the document declares (`theme brand extends dark { … }`); absent when none. */
  readonly themes?: readonly ThemeDef[];
}

/**
 * A theme a document declares or a library ships: theme keys (`background`, `edge`,
 * `fontFamily`, …) as strings, and per-kind node colours.
 */
export interface ThemeDef {
  readonly name: string;
  /** A built-in, another declared theme or `alias.name`; `default` when absent. */
  readonly extends?: string;
  readonly nodes?: Readonly<
    Record<string, { readonly fill?: string; readonly stroke?: string; readonly text?: string }>
  >;
  readonly [key: string]: unknown;
}

/**
 * Every change to a model, from the text editor or the canvas, is one of these typed operations.
 * Applying an operation returns the new model and the inverse operation that undoes it.
 */
export type Operation =
  | { readonly type: 'addElement'; readonly element: Element }
  | { readonly type: 'removeElement'; readonly id: ElementId }
  | { readonly type: 'renameElement'; readonly id: ElementId; readonly name: string }
  /**
   * Gives element `id` the id `to`, which keeps its parent (only the part after the last dot
   * changes). Its descendants and every reference follow; `relationships` and `groups` change
   * the ids derived from it at the same time (old id to new), and `name`, when given, is its name
   * afterwards. Build it with `Session.renameElement`, which works these out.
   */
  | {
      readonly type: 'changeElementId';
      readonly id: ElementId;
      readonly to: ElementId;
      readonly relationships?: readonly {
        readonly from: RelationshipId;
        readonly to: RelationshipId;
      }[];
      readonly groups?: readonly { readonly from: GroupId; readonly to: GroupId }[];
      readonly name?: string;
    }
  /**
   * Sets an element's kind, name, technology, shape, tags, description, style and properties
   * to `element`'s (an inspector's edit). Its id, parent, group and ports must stay as they are.
   */
  | { readonly type: 'updateElement'; readonly element: Element }
  | { readonly type: 'addRelationship'; readonly relationship: Relationship }
  | { readonly type: 'removeRelationship'; readonly id: RelationshipId }
  /**
   * Moves a relationship's ends to `source` and `target`. `renames` changes relationship ids at
   * the same time (old id to new, all at once), and every `route` naming them follows: the text
   * derives unnamed ids from endpoints (`a->b`), so they change when an end moves.
   */
  | {
      readonly type: 'reconnectRelationship';
      readonly id: RelationshipId;
      readonly source: Endpoint;
      readonly target: Endpoint;
      readonly renames?: Readonly<Record<RelationshipId, RelationshipId>>;
    }
  | {
      readonly type: 'setRelationshipLabel';
      readonly id: RelationshipId;
      readonly label: string | undefined;
    }
  /**
   * Sets a relationship's label, technology, arrow (line and direction), tags, style and
   * properties to `relationship`'s (an inspector's edit). Its id and ends must stay as they are.
   */
  | { readonly type: 'updateRelationship'; readonly relationship: Relationship }
  /** Gives relationships new ids, all at once, and moves the views' routes along with them. */
  | {
      readonly type: 'renameRelationships';
      readonly renames: readonly { readonly from: RelationshipId; readonly to: RelationshipId }[];
    }
  | { readonly type: 'addGroup'; readonly group: Group }
  | { readonly type: 'removeGroup'; readonly id: GroupId }
  | { readonly type: 'addView'; readonly view: View }
  | { readonly type: 'removeView'; readonly id: ViewId }
  /** Gives a view a new id; it keeps its place among the views and everything it holds. */
  | { readonly type: 'renameView'; readonly id: ViewId; readonly to: ViewId }
  /** Replaces a view wholesale; mostly the inverse of edits that touch several parts of it. */
  | { readonly type: 'replaceView'; readonly view: View }
  | { readonly type: 'setLayout'; readonly view: ViewId; readonly layout: LayoutOptions }
  | {
      readonly type: 'addHint';
      readonly view: ViewId;
      readonly hint: LayoutHint;
      /** Position in the view's hint list; appends when omitted. */
      readonly index?: number;
    }
  | { readonly type: 'removeHint'; readonly view: ViewId; readonly index: number }
  /** Rewrites one hint where it stands. */
  | {
      readonly type: 'replaceHint';
      readonly view: ViewId;
      readonly index: number;
      readonly hint: LayoutHint;
    }
  /** Adds a `use` of a library; the alias must be free. */
  | { readonly type: 'useLibrary'; readonly use: LibraryUse; readonly index?: number }
  | { readonly type: 'unuseLibrary'; readonly alias: string }
  /** Adds a `style` block at `index` in `model.styles`; appends when omitted. */
  | { readonly type: 'addStyleRule'; readonly rule: StyleRule; readonly index?: number }
  | { readonly type: 'removeStyleRule'; readonly index: number }
  /** Replaces the `style` block at `index`: its selector and its keys. */
  | { readonly type: 'updateStyleRule'; readonly index: number; readonly rule: StyleRule }
  /**
   * `theme name` for the document, or for one view when `view` is set; without `theme` the
   * statement is removed.
   */
  | { readonly type: 'setTheme'; readonly view?: ViewId; readonly theme?: string }
  /** Declares a theme at `index` in `model.themes` (appends when omitted); the name must be free. */
  | { readonly type: 'addTheme'; readonly theme: ThemeDef; readonly index?: number }
  | { readonly type: 'removeTheme'; readonly name: string }
  /** Replaces a declared theme's `extends`, keys and node colours; its name stays. */
  | { readonly type: 'updateTheme'; readonly theme: ThemeDef }
  | { readonly type: 'batch'; readonly operations: readonly Operation[] };

export interface Point {
  readonly x: number;
  readonly y: number;
}

export interface Size {
  readonly width: number;
  readonly height: number;
}

export interface Box extends Point, Size {}

export interface EdgeRoute {
  /** Start point, bend points and end point, in absolute coordinates. */
  readonly points: readonly Point[];
  readonly label?: Box;
}
