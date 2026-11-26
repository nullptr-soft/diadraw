import { IDBFactory } from 'fake-indexeddb';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { cleanName, DiagramLibrary, describeTime } from './library';

describe('diagram library', () => {
  let factory: IDBFactory;
  let library: DiagramLibrary;

  beforeEach(async () => {
    factory = new IDBFactory();
    library = await DiagramLibrary.open(factory);
  });
  afterEach(() => library.close());

  it('starts empty', async () => {
    expect(await library.list()).toEqual([]);
    expect(await library.readDraft()).toBeUndefined();
  });

  it('saves, lists newest first, and reads back', async () => {
    const a = await library.create('Login flow', 'model {\n}\n', 1000);
    const b = await library.create('  Checkout  ', 'model { a = system }\n', 2000);
    expect(b.name).toBe('Checkout');
    const list = await library.list();
    expect(list.map((d) => d.name)).toEqual(['Checkout', 'Login flow']);
    expect(list[1]).toEqual({
      id: a.id,
      name: 'Login flow',
      created: 1000,
      updated: 1000,
      size: 10,
    });
    expect(list[0]).not.toHaveProperty('text');
    expect((await library.get(a.id))?.text).toBe('model {\n}\n');
  });

  it('saves new text in place and keeps the name and creation time', async () => {
    const a = await library.create('Login flow', 'one', 1000);
    const saved = await library.save(a.id, 'two', 'ignored', 5000);
    expect(saved).toEqual({
      id: a.id,
      name: 'Login flow',
      text: 'two',
      created: 1000,
      updated: 5000,
    });
    expect(await library.get(a.id)).toEqual(saved);
    expect(await library.list()).toHaveLength(1);
  });

  it('brings back a diagram deleted meanwhile when it is saved again', async () => {
    const a = await library.create('Login flow', 'one', 1000);
    await library.remove(a.id);
    const saved = await library.save(a.id, 'two', 'Login flow', 3000);
    expect(saved).toMatchObject({ id: a.id, name: 'Login flow', text: 'two', created: 3000 });
  });

  it('renames, and reports a missing diagram', async () => {
    const a = await library.create('Login flow', 'one', 1000);
    expect(await library.rename(a.id, ' Sign-in ', 2000)).toMatchObject({
      name: 'Sign-in',
      text: 'one',
      updated: 2000,
    });
    expect(await library.rename('nope', 'x')).toBeUndefined();
    expect(await library.get('nope')).toBeUndefined();
  });

  it('deletes', async () => {
    const a = await library.create('A', 'one');
    const b = await library.create('B', 'two');
    await library.remove(a.id);
    expect((await library.list()).map((d) => d.id)).toEqual([b.id]);
    expect(await library.get(a.id)).toBeUndefined();
  });

  it('keeps the working copy across reopening the database', async () => {
    await library.writeDraft({ text: 'draft', name: 'x.draw', saved: 'x', libraryId: 'id-1' });
    await library.writeDraft({ text: 'draft 2' });
    library.close();
    library = await DiagramLibrary.open(factory);
    expect(await library.readDraft()).toEqual({ text: 'draft 2' });
  });

  it('keeps diagrams across reopening the database', async () => {
    const a = await library.create('Kept', 'text');
    library.close();
    library = await DiagramLibrary.open(factory);
    expect((await library.get(a.id))?.text).toBe('text');
  });
});

describe('names and times', () => {
  it('names an unnamed diagram Untitled', () => {
    expect(cleanName('   ')).toBe('Untitled');
    expect(cleanName(' a ')).toBe('a');
  });

  it('describes recent times relatively', () => {
    const now = new Date(2026, 10, 23, 14, 30).getTime();
    expect(describeTime(now - 10_000, now)).toBe('just now');
    expect(describeTime(now - 5 * 60_000, now)).toBe('5 min ago');
    expect(describeTime(now - 2 * 3600_000, now)).toMatch(/^today /);
    expect(describeTime(now - 3 * 86_400_000, now)).not.toMatch(/^today /);
  });
});
