/**
 * Where diagrams can be kept besides files: this browser (IndexedDB), Google Drive and OneDrive,
 * behind one interface so that one dialog lists, opens, saves, renames and deletes in any of them.
 */

import type { CloudDrive, CloudId } from './cloud';
import { cloudLabel } from './cloud';
import type { DiagramLibrary } from './library';

export type LocationId = 'browser' | CloudId;

/** A diagram in a location, without its text. */
export interface StoredFile {
  readonly id: string;
  /** As the location shows it: `name.draw` in the clouds, a plain name in the browser. */
  readonly name: string;
  /** Milliseconds since the epoch. */
  readonly updated: number;
  readonly size?: number;
}

export interface StorageLocation {
  readonly id: LocationId;
  readonly label: string;
  /** What deleting does there, for the button's title and the confirmation. */
  readonly removeHint: string;
  /** The account, for the clouds; the browser needs none. */
  readonly cloud?: CloudDrive;
  list(): Promise<StoredFile[]>;
  read(id: string): Promise<{ file: StoredFile; text: string } | undefined>;
  create(name: string, text: string): Promise<StoredFile>;
  /** Replaces a diagram's text; `name` brings back one deleted meanwhile, where that is possible. */
  save(id: string, text: string, name: string): Promise<StoredFile>;
  rename(id: string, name: string): Promise<StoredFile | undefined>;
  remove(id: string): Promise<void>;
}

/** The browser's IndexedDB library as a location. */
export function browserLocation(library: DiagramLibrary): StorageLocation {
  const summary = (d: {
    id: string;
    name: string;
    updated: number;
    text?: string;
  }): StoredFile => ({
    id: d.id,
    name: d.name,
    updated: d.updated,
    ...(d.text !== undefined && { size: d.text.length }),
  });
  return {
    id: 'browser',
    label: 'This browser',
    removeHint: 'Delete it from this browser. This cannot be undone.',
    list: async () => (await library.list()).map((d) => ({ ...summary(d), size: d.size })),
    read: async (id) => {
      const d = await library.get(id);
      return d && { file: summary(d), text: d.text };
    },
    create: async (name, text) => summary(await library.create(name, text)),
    save: async (id, text, name) => summary(await library.save(id, text, name)),
    rename: async (id, name) => {
      const d = await library.rename(id, name);
      return d && summary(d);
    },
    remove: (id) => library.remove(id),
  };
}

/** A cloud drive as a location. */
export function cloudLocation(drive: CloudDrive): StorageLocation {
  const id = drive.account.id;
  return {
    id,
    label: cloudLabel(id),
    removeHint:
      id === 'gdrive'
        ? 'Move it to the Google Drive trash (restorable there for 30 days).'
        : 'Move it to the OneDrive recycle bin.',
    cloud: drive,
    list: () => drive.list(),
    read: (fileId) => drive.read(fileId),
    create: (name, text) => drive.create(name, text),
    save: (fileId, text) => drive.save(fileId, text),
    rename: (fileId, name) => drive.rename(fileId, name),
    remove: (fileId) => drive.remove(fileId),
  };
}

/** The file name a stored diagram goes by in the editor (title bar, Save As, exports). */
export const editorFileName = (name: string): string =>
  /\.draw$/i.test(name) ? name : `${name}.draw`;

/** A stored diagram's name without `.draw`, for the name field. */
export const plainName = (name: string): string => name.replace(/\.draw$/i, '');
