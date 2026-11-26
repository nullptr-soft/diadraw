/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** OAuth client ID of a Google Cloud "Web application" client (docs/guide/cloud-storage.md). */
  readonly VITE_GOOGLE_CLIENT_ID?: string;
  /** Application (client) ID of a Microsoft Entra app with a single-page application platform. */
  readonly VITE_ONEDRIVE_CLIENT_ID?: string;
}
