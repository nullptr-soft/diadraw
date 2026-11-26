/**
 * Node's file system as the core reads it (`DiaDraw.setFileSystem`), for hosts running in Node,
 * such as the VS Code extension.
 */
import { readdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import type { HostFileSystem } from './index.js';

export function nodeFileSystem(env: NodeJS.ProcessEnv = process.env): HostFileSystem {
  return {
    read: (path) => {
      try {
        return readFileSync(path);
      } catch {
        return undefined;
      }
    },
    list: (dir) => {
      try {
        return readdirSync(dir);
      } catch {
        return undefined;
      }
    },
    join: (dir, name) => join(dir, name),
    parent: (path) => dirname(path),
    env: (name) => env[name],
  };
}
