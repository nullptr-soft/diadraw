import type { LocationId, StorageLocation, StoredFile } from './storage';
import { plainName } from './storage';
import { describeTime } from './library';

const TEMPLATE = `
  <dialog aria-labelledby="dd-storage-title">
    <form method="dialog" class="library-head">
      <h2 id="dd-storage-title">Open or save</h2>
      <button type="submit" aria-label="Close">✕</button>
    </form>
    <div class="storage-tabs" role="tablist" aria-label="Where"></div>
    <div class="storage-account" hidden>
      <span></span>
      <button type="button" data-op="sign-out">Sign out</button>
    </div>
    <div class="storage-sign-in" hidden>
      <span></span>
      <button type="button" data-op="sign-in"></button>
    </div>
    <form class="library-save">
      <input name="name" aria-label="Name" placeholder="Name" required />
      <button type="submit">Save current diagram</button>
    </form>
    <p class="storage-error" role="alert" hidden></p>
    <p class="library-empty" hidden></p>
    <ul class="library-list"></ul>
    <p class="library-note"></p>
  </dialog>
`;

const NOTES: Record<LocationId, string> = {
  browser:
    "Diagrams saved here stay in this browser on this device. Clearing the site's data deletes them; save to a file or a cloud drive to keep a copy elsewhere.",
  gdrive:
    'Diagrams are saved as .draw files in My Drive. DiaDraw can only see the files it saved there itself.',
  onedrive:
    'Diagrams are saved as .draw files in your OneDrive, in the Apps › DiaDraw folder. Files you put in that folder show up here too.',
};

const errorMessage = (error: unknown): string =>
  error instanceof Error ? error.message : String(error);

/**
 * `<dd-storage>`: the diagrams kept in this browser, in Google Drive and in OneDrive, one tab
 * each, in a modal dialog. It signs in and out, lists, renames and deletes itself; opening a
 * diagram and saving the current one are the app's, through `dd-storage-open` ({ location, id })
 * and `dd-storage-save` ({ location, name }). Renaming and deleting fire `dd-storage-renamed`
 * ({ location, id, name }) and `dd-storage-removed` ({ location, id }) so the app can follow.
 */
export class StoragePanel extends HTMLElement {
  #locations: StorageLocation[] = [];
  #current: LocationId = 'browser';
  /** The diagram being edited (where and which), highlighted in its list. */
  editing: { location: LocationId; id: string } | undefined;
  #dialog!: HTMLDialogElement;
  #list!: HTMLUListElement;
  #tabs!: HTMLElement;
  /** Bumped on every refresh, so a slow list from an earlier tab is dropped. */
  #generation = 0;

  connectedCallback(): void {
    if (this.#dialog) return;
    this.innerHTML = TEMPLATE;
    this.#dialog = this.querySelector('dialog')!;
    this.#list = this.querySelector('.library-list')!;
    this.#tabs = this.querySelector('.storage-tabs')!;
    const save = this.querySelector<HTMLFormElement>('.library-save')!;
    save.addEventListener('submit', (e) => {
      e.preventDefault();
      const name = (save.elements.namedItem('name') as HTMLInputElement).value.trim();
      if (name) this.#emit('dd-storage-save', { location: this.#current, name });
    });
    this.#list.addEventListener('click', (e) => void this.#onClick(e));
    this.#tabs.addEventListener('click', (e) => {
      const tab = (e.target as Element).closest<HTMLButtonElement>('[role="tab"]');
      if (tab?.dataset.location) void this.select(tab.dataset.location as LocationId);
    });
    this.#tabs.addEventListener('keydown', (e) => {
      if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
      const ids = this.#locations.map((l) => l.id);
      const step = e.key === 'ArrowRight' ? 1 : -1;
      const next = ids[(ids.indexOf(this.#current) + step + ids.length) % ids.length];
      if (next) {
        void this.select(next);
        this.#tabs.querySelector<HTMLElement>(`[data-location="${next}"]`)?.focus();
      }
    });
    this.querySelector('[data-op="sign-in"]')!.addEventListener('click', () => void this.#signIn());
    this.querySelector('[data-op="sign-out"]')!.addEventListener('click', () => {
      this.#location?.cloud?.account.signOut();
      void this.refresh();
    });
    // A click on the backdrop closes the dialog.
    this.#dialog.addEventListener('click', (e) => {
      if (e.target === this.#dialog) this.close();
    });
  }

  set locations(locations: StorageLocation[]) {
    this.#locations = locations;
    if (!locations.some((l) => l.id === this.#current))
      this.#current = locations[0]?.id ?? 'browser';
    this.#renderTabs();
  }

  get locations(): StorageLocation[] {
    return this.#locations;
  }

  get isOpen(): boolean {
    return this.#dialog?.open ?? false;
  }

  get #location(): StorageLocation | undefined {
    return this.#locations.find((l) => l.id === this.#current);
  }

  /** Shows the dialog on `location` (or the last one), with `suggestedName` in the name field. */
  async show(suggestedName: string, location?: LocationId, message?: string): Promise<void> {
    const input = this.querySelector<HTMLInputElement>('.library-save input')!;
    input.value = suggestedName;
    if (location && this.#locations.some((l) => l.id === location)) this.#current = location;
    if (!this.#dialog.open) this.#dialog.showModal();
    await this.refresh();
    if (message) this.#showError(message);
    if (!this.querySelector<HTMLElement>('.library-save')!.hidden) {
      input.focus();
      input.select();
    }
  }

  close(): void {
    this.#dialog.close();
  }

  async select(location: LocationId): Promise<void> {
    this.#current = location;
    await this.refresh();
  }

  async refresh(): Promise<void> {
    const generation = ++this.#generation;
    this.#renderTabs();
    const location = this.#location;
    const account = location?.cloud?.account;
    const signedIn = !account || account.signedIn;
    const accountRow = this.querySelector<HTMLElement>('.storage-account')!;
    accountRow.hidden = !account || !signedIn;
    accountRow.querySelector('span')!.textContent = account ? `Signed in to ${account.label}` : '';
    const signIn = this.querySelector<HTMLElement>('.storage-sign-in')!;
    signIn.hidden = !account || signedIn || !account.configured;
    signIn.querySelector('button')!.textContent = account ? `Sign in to ${account.label}` : '';
    signIn.querySelector('span')!.textContent = account
      ? `Sign in to open and save diagrams in ${account.label}.`
      : '';
    this.querySelector<HTMLElement>('.library-save')!.hidden = !location || !signedIn;
    this.querySelector<HTMLElement>('.library-save button')!.title = location
      ? `Save a copy of the current diagram in ${location.id === 'browser' ? 'this browser' : location.label}`
      : '';
    const note = this.querySelector<HTMLElement>('.library-note')!;
    note.textContent = !location
      ? ''
      : account && !account.configured
        ? `This copy of DiaDraw is not set up for ${account.label}: it was built without a ${account.label} client ID.`
        : NOTES[location.id];
    this.#showError(undefined);
    const empty = this.querySelector<HTMLElement>('.library-empty')!;
    if (!location || !signedIn) {
      empty.hidden = true;
      this.#list.replaceChildren();
      return;
    }
    empty.hidden = false;
    empty.textContent = 'Loading…';
    let items: StoredFile[];
    try {
      items = await location.list();
    } catch (error) {
      if (generation !== this.#generation) return;
      empty.hidden = true;
      this.#list.replaceChildren();
      this.#showError(errorMessage(error));
      // A sign-in that ran out shows the Sign in button again.
      if (account && !account.signedIn) void this.refresh();
      return;
    }
    if (generation !== this.#generation) return;
    empty.hidden = items.length > 0;
    empty.textContent =
      location.id === 'browser'
        ? 'No diagrams saved in this browser yet.'
        : `No diagrams saved in ${location.label} yet.`;
    this.#list.replaceChildren(...items.map((item) => this.#row(location, item)));
  }

  #renderTabs(): void {
    if (!this.#tabs) return;
    this.#tabs.replaceChildren(
      ...this.#locations.map((l) => {
        const tab = Object.assign(document.createElement('button'), {
          type: 'button',
          textContent: l.label,
        });
        tab.setAttribute('role', 'tab');
        tab.dataset.location = l.id;
        const selected = l.id === this.#current;
        tab.setAttribute('aria-selected', String(selected));
        tab.tabIndex = selected ? 0 : -1;
        return tab;
      }),
    );
  }

  async #signIn(): Promise<void> {
    const account = this.#location?.cloud?.account;
    if (!account) return;
    try {
      await account.signIn();
    } catch (error) {
      this.#showError(errorMessage(error));
      return;
    }
    await this.refresh();
    this.#emit('dd-storage-signed-in', { location: account.id });
  }

  #showError(message: string | undefined): void {
    const p = this.querySelector<HTMLElement>('.storage-error')!;
    p.hidden = !message;
    p.textContent = message ?? '';
  }

  #row(location: StorageLocation, item: StoredFile): HTMLLIElement {
    const li = document.createElement('li');
    li.dataset.id = item.id;
    const editing = this.editing;
    if (editing?.location === location.id && editing.id === item.id) {
      li.setAttribute('aria-current', 'true');
    }
    const name = plainName(item.name);
    const open = Object.assign(document.createElement('button'), {
      type: 'button',
      className: 'library-open',
      textContent: name,
      title: `Open ${item.name}`,
    });
    open.dataset.op = 'open';
    open.dataset.name = name;
    const time = Object.assign(document.createElement('time'), {
      dateTime: new Date(item.updated).toISOString(),
      textContent: describeTime(item.updated),
      title: new Date(item.updated).toLocaleString(),
    });
    const button = (op: string, label: string, title: string) => {
      const b = Object.assign(document.createElement('button'), {
        type: 'button',
        textContent: label,
        title,
      });
      b.dataset.op = op;
      return b;
    };
    li.append(
      open,
      time,
      button('rename', 'Rename', `Rename ${name}`),
      button('remove', 'Delete', `${location.removeHint.replace(/\.$/, '')}: ${name}`),
    );
    return li;
  }

  async #onClick(e: Event): Promise<void> {
    const button = (e.target as Element).closest<HTMLButtonElement>('button[data-op]');
    const li = button?.closest('li');
    const id = li?.dataset.id;
    const location = this.#location;
    if (!button || !li || !id || !location) return;
    const name = li.querySelector<HTMLElement>('.library-open')?.dataset.name ?? '';
    try {
      switch (button.dataset.op) {
        case 'open':
          this.#emit('dd-storage-open', { location: location.id, id });
          break;
        case 'rename':
          this.#startRename(location, li, id, name);
          break;
        case 'remove':
          if (!confirm(`Delete “${name}”? ${location.removeHint}`)) return;
          await location.remove(id);
          this.#emit('dd-storage-removed', { location: location.id, id });
          await this.refresh();
          break;
      }
    } catch (error) {
      this.#showError(errorMessage(error));
    }
  }

  /** Swaps the name for a field: Enter or leaving the field renames, Escape cancels. */
  #startRename(location: StorageLocation, li: HTMLLIElement, id: string, name: string): void {
    const input = Object.assign(document.createElement('input'), {
      value: name,
      className: 'library-rename',
    });
    input.setAttribute('aria-label', 'New name');
    li.querySelector('.library-open')!.replaceWith(input);
    input.focus();
    input.select();
    let finished = false;
    const finish = async (commit: boolean) => {
      if (finished) return;
      finished = true;
      const next = input.value.trim();
      try {
        if (commit && next && next !== name) {
          const renamed = await location.rename(id, next);
          if (renamed) {
            this.#emit('dd-storage-renamed', { location: location.id, id, name: renamed.name });
          }
        }
        await this.refresh();
      } catch (error) {
        await this.refresh();
        this.#showError(errorMessage(error));
      }
    };
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        void finish(true);
      } else if (e.key === 'Escape') {
        // Cancels the rename, not the dialog.
        e.preventDefault();
        e.stopPropagation();
        void finish(false);
      }
    });
    input.addEventListener('blur', () => void finish(true));
  }

  #emit(type: string, detail: unknown): void {
    this.dispatchEvent(new CustomEvent(type, { detail, bubbles: true }));
  }
}

customElements.define('dd-storage', StoragePanel);

declare global {
  interface HTMLElementTagNameMap {
    'dd-storage': StoragePanel;
  }
}
