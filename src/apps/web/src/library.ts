/**
 * Diagrams kept in the browser's IndexedDB: a small library of named documents (save, open,
 * rename, delete, list), and the working copy of whatever is being edited, so closing or reloading
 * the tab does not lose it.
 *
 * Everything lives in one database, `diadraw`, on this origin and in this browser profile only.
 */

const DB_NAME = 'diadraw';
const DB_VERSION = 1;
const DIAGRAMS = 'diagrams';
const SESSION = 'session';
const DRAFT_KEY = 'draft';

export interface StoredDiagram {
  readonly id: string;
  readonly name: string;
  readonly text: string;
  /** Milliseconds since the epoch. */
  readonly created: number;
  readonly updated: number;
}

/** A library entry without its text, for listing. */
export type DiagramSummary = Omit<StoredDiagram, 'text'> & { readonly size: number };

/** The working copy: the editor's text and where it came from. */
export interface Draft {
  readonly text: string;
  /** The file or library name the text came from, if any. */
  readonly name?: string;
  /**
   * Where the text was opened from or saved to besides a file: this browser's library, Google
   * Drive or OneDrive, and the diagram's id there.
   */
  readonly location?: 'browser' | 'gdrive' | 'onedrive';
  readonly fileId?: string;
  /** Before cloud storage: the library entry (now `location: 'browser'` and `fileId`). */
  readonly libraryId?: string;
  /** The text as last opened or saved, to tell whether the working copy has unsaved changes. */
  readonly saved?: string;
}

const request = <T>(r: IDBRequest<T>): Promise<T> =>
  new Promise((resolve, reject) => {
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error ?? new Error('IndexedDB request failed'));
  });

const done = (tx: IDBTransaction): Promise<void> =>
  new Promise((resolve, reject) => {
    tx.oncomplete = () => resolve();
    tx.onabort = tx.onerror = () => reject(tx.error ?? new Error('IndexedDB transaction failed'));
  });

const newId = (): string =>
  typeof crypto !== 'undefined' && 'randomUUID' in crypto
    ? crypto.randomUUID()
    : `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;

/** Trims a name and falls back to "Untitled" for an empty one. */
export const cleanName = (name: string): string => name.trim() || 'Untitled';

export class DiagramLibrary {
  readonly #db: IDBDatabase;

  private constructor(db: IDBDatabase) {
    this.#db = db;
  }

  /** Opens (and on first use creates) the database. Rejects where IndexedDB is unavailable. */
  static async open(factory: IDBFactory = globalThis.indexedDB): Promise<DiagramLibrary> {
    if (!factory) throw new Error('IndexedDB is not available');
    const open = factory.open(DB_NAME, DB_VERSION);
    open.onupgradeneeded = () => {
      const db = open.result;
      if (!db.objectStoreNames.contains(DIAGRAMS)) {
        db.createObjectStore(DIAGRAMS, { keyPath: 'id' }).createIndex('updated', 'updated');
      }
      if (!db.objectStoreNames.contains(SESSION)) db.createObjectStore(SESSION);
    };
    const db = await request(open);
    // Another tab upgrading the database: step aside rather than block it.
    db.onversionchange = () => db.close();
    return new DiagramLibrary(db);
  }

  close(): void {
    this.#db.close();
  }

  #store(name: string, mode: IDBTransactionMode): { store: IDBObjectStore; tx: IDBTransaction } {
    const tx = this.#db.transaction(name, mode);
    return { store: tx.objectStore(name), tx };
  }

  /** Every saved diagram, most recently changed first. */
  async list(): Promise<DiagramSummary[]> {
    const { store } = this.#store(DIAGRAMS, 'readonly');
    const all = await request(store.getAll() as IDBRequest<StoredDiagram[]>);
    return all
      .map(({ text, ...rest }) => ({ ...rest, size: text.length }))
      .sort((a, b) => b.updated - a.updated || a.name.localeCompare(b.name));
  }

  async get(id: string): Promise<StoredDiagram | undefined> {
    const { store } = this.#store(DIAGRAMS, 'readonly');
    return (await request(store.get(id) as IDBRequest<StoredDiagram | undefined>)) ?? undefined;
  }

  /** Saves `text` as a new diagram. */
  async create(name: string, text: string, now = Date.now()): Promise<StoredDiagram> {
    const diagram: StoredDiagram = {
      id: newId(),
      name: cleanName(name),
      text,
      created: now,
      updated: now,
    };
    const { store, tx } = this.#store(DIAGRAMS, 'readwrite');
    store.add(diagram);
    await done(tx);
    return diagram;
  }

  /**
   * Replaces a diagram's text. A diagram deleted meanwhile (in another tab) comes back under
   * `fallbackName`, so a save never silently goes nowhere.
   */
  async save(
    id: string,
    text: string,
    fallbackName = 'Untitled',
    now = Date.now(),
  ): Promise<StoredDiagram> {
    return this.#update(id, (old) => ({
      id,
      name: old?.name ?? cleanName(fallbackName),
      text,
      created: old?.created ?? now,
      updated: now,
    }));
  }

  /** Renames a diagram; resolves to undefined if there is no such diagram. */
  async rename(id: string, name: string, now = Date.now()): Promise<StoredDiagram | undefined> {
    let found = false;
    const result = await this.#update(id, (old) => {
      if (!old) return undefined;
      found = true;
      return { ...old, name: cleanName(name), updated: now };
    });
    return found ? result : undefined;
  }

  async remove(id: string): Promise<void> {
    const { store, tx } = this.#store(DIAGRAMS, 'readwrite');
    store.delete(id);
    await done(tx);
  }

  /** Reads and writes one record in a single transaction. */
  async #update(
    id: string,
    change: (old: StoredDiagram | undefined) => StoredDiagram | undefined,
  ): Promise<StoredDiagram> {
    const { store, tx } = this.#store(DIAGRAMS, 'readwrite');
    let next: StoredDiagram | undefined;
    const get = store.get(id) as IDBRequest<StoredDiagram | undefined>;
    get.onsuccess = () => {
      next = change(get.result ?? undefined);
      if (next) store.put(next);
    };
    await done(tx);
    return next!;
  }

  async readDraft(): Promise<Draft | undefined> {
    const { store } = this.#store(SESSION, 'readonly');
    const draft = (await request(store.get(DRAFT_KEY))) as Draft | undefined;
    return draft && typeof draft.text === 'string' ? draft : undefined;
  }

  async writeDraft(draft: Draft): Promise<void> {
    const { store, tx } = this.#store(SESSION, 'readwrite');
    store.put(draft, DRAFT_KEY);
    await done(tx);
  }
}

/** "just now", "5 min ago", "today 14:05", or a date. */
export function describeTime(time: number, now = Date.now()): string {
  const seconds = Math.round((now - time) / 1000);
  if (seconds < 60) return 'just now';
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`;
  const date = new Date(time);
  const hm = date.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' });
  if (new Date(now).toDateString() === date.toDateString()) return `today ${hm}`;
  return `${date.toLocaleDateString()} ${hm}`;
}
