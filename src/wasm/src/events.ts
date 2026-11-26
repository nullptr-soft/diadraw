/** A minimal typed event emitter: what the core reports, fanned out to listeners. */
export class Emitter<Events> {
  readonly #listeners = new Map<keyof Events, Set<(event: never) => void>>();

  on<K extends keyof Events>(type: K, listener: (event: Events[K]) => void): () => void {
    let set = this.#listeners.get(type);
    if (!set) this.#listeners.set(type, (set = new Set()));
    set.add(listener);
    return () => {
      set.delete(listener);
    };
  }

  emit<K extends keyof Events>(type: K, event: Events[K]): void {
    for (const listener of [...(this.#listeners.get(type) ?? [])])
      (listener as (event: Events[K]) => void)(event);
  }

  clear(): void {
    this.#listeners.clear();
  }
}
