/** The Language Server Protocol's base protocol: `Content-Length` headers, then a JSON body. */
export class MessageReader {
  #buffer = Buffer.alloc(0);

  constructor(private readonly onMessage: (body: string) => void) {}

  push(chunk: Buffer): void {
    this.#buffer = Buffer.concat([this.#buffer, chunk]);
    for (;;) {
      const end = this.#buffer.indexOf('\r\n\r\n');
      if (end < 0) return;
      const headers = this.#buffer.subarray(0, end).toString('ascii');
      const length = Number(/content-length:\s*(\d+)/i.exec(headers)?.[1] ?? NaN);
      if (!Number.isFinite(length)) {
        this.#buffer = this.#buffer.subarray(end + 4); // a header block without a length: skip it
        continue;
      }
      if (this.#buffer.length < end + 4 + length) return;
      const body = this.#buffer.subarray(end + 4, end + 4 + length).toString('utf8');
      this.#buffer = this.#buffer.subarray(end + 4 + length);
      this.onMessage(body);
    }
  }
}
