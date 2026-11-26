# Google Drive and OneDrive

DiaDraw can open and save diagrams in Google Drive and OneDrive, in every app: the web app, and
the macOS, Linux and Windows apps. A diagram saved from one app opens in the others, because they
all use the same place in each service.

## Using it

- **Web app:** the **Storage…** button in the toolbar opens one dialog with a tab for each place a
  diagram can live: **This browser** (the browser's own storage, as before), **Google Drive** and
  **OneDrive**.
- **macOS, Linux and Windows:** **File › Cloud Storage…** opens the same dialog with the
  Google Drive and OneDrive tabs (these apps keep diagrams in files, so they have no browser tab).

In a cloud tab, **Sign in** opens the service's sign-in page (a popup on the web, your browser in
the desktop apps). Once signed in, the tab lists your diagrams there, newest first. From the list
you can:

- **Open** a diagram. In the web app it replaces the one being edited; in the desktop apps it
  opens in a new window, unless the current window is an untouched new document.
- **Save current diagram** under a new name. From then on, ⌘S / Ctrl+S saves back to the same
  file in that service, and the title shows where it lives, such as “Checkout (OneDrive)”.
- **Rename** and **Delete** a diagram. Deleting moves it to the Google Drive trash or the
  OneDrive recycle bin, from where it can be restored.

If a sign-in has run out when you save, the dialog opens on that service and asks you to sign in
again; then save once more.

## Where diagrams go, and what DiaDraw can see

DiaDraw asks each service for the least access it offers:

| Service      | Access DiaDraw asks for                                              | Where diagrams are saved                       |
| ------------ | -------------------------------------------------------------------- | ---------------------------------------------- |
| Google Drive | `drive.file`: only the files DiaDraw itself created or opened        | `<name>.draw` in the root of My Drive          |
| OneDrive     | `Files.ReadWrite.AppFolder`: only DiaDraw's own folder in your drive | `<name>.draw` in the **Apps › DiaDraw** folder |

So DiaDraw cannot read your other files. In OneDrive, `.draw` files you copy into
**Apps › DiaDraw** yourself show up in DiaDraw too; in Google Drive, only the files DiaDraw saved
do.

The desktop apps keep a sign-in in memory until they quit. The web app keeps it for the browser
tab (in its session storage), so a reload stays signed in, and forgets it when the tab closes.

## Setting it up for a build of DiaDraw

Each service only talks to apps registered with it, so whoever builds and ships DiaDraw registers
it once with Google and with Microsoft and builds the IDs in. A build without them shows the tab
with “not set up” in place of the Sign in button. The IDs are not secrets.

### Google Drive

In the [Google Cloud console](https://console.cloud.google.com/):

1. Create a project (for example “DiaDraw”).
2. **APIs & Services › Library:** enable the **Google Drive API**.
3. **Google Auth Platform › Branding:** the app name, a support address and the home page
   (the site's address). **Audience:** External; while the app is in “Testing”, only the test
   users listed there can sign in, so publish it when it should work for everyone.
4. **Data Access:** add the scope `https://www.googleapis.com/auth/drive.file`. It is a
   non-sensitive scope, so the app does not need Google's security review.
5. **Clients › Create client › Web application** (for the web app):
   - Authorised JavaScript origins: the site's origin, e.g. `https://mykulyak.github.io`, and
     `http://localhost:5173` for `npm run dev:web`.
   - Authorised redirect URIs: the app's `oauth.html`, e.g.
     `https://mykulyak.github.io/diadraw/app/oauth.html`, and `http://localhost:5173/oauth.html`.
   - Its **client ID** is `VITE_GOOGLE_CLIENT_ID` below.
6. **Clients › Create client › Desktop app** (for the macOS, Linux and Windows apps). Its
   **client ID** and **client secret** are `DIADRAW_GOOGLE_CLIENT_ID` and
   `DIADRAW_GOOGLE_CLIENT_SECRET` below. (Google issues a secret even for desktop apps; it ships in
   every copy, so it is not secret, and the sign-in is protected by PKCE instead.)

### OneDrive

In the [Microsoft Entra admin centre](https://entra.microsoft.com/), **App registrations › New
registration**:

1. Name: “DiaDraw”. Supported account types: **Accounts in any organizational directory and
   personal Microsoft accounts**.
2. **Authentication › Add a platform › Single-page application** (for the web app), with the
   redirect URIs `https://mykulyak.github.io/diadraw/app/oauth.html` and
   `http://localhost:5173/oauth.html`.
3. **Authentication › Add a platform › Mobile and desktop applications** (for the desktop apps),
   with the custom redirect URI `http://localhost`. (Any port is accepted on `localhost`.)
4. **API permissions › Add a permission › Microsoft Graph › Delegated:** `Files.ReadWrite.AppFolder`
   and `offline_access`.
5. The **Application (client) ID** on the overview page is both `VITE_ONEDRIVE_CLIENT_ID` and
   `DIADRAW_ONEDRIVE_CLIENT_ID` below. There is no secret.

### Building the IDs in

- **Web app:** set `VITE_GOOGLE_CLIENT_ID` and `VITE_ONEDRIVE_CLIENT_ID` in the environment of the
  build (or in `src/apps/web/.env.local` for `npm run dev:web`). The GitHub Pages workflow takes
  them from the repository variables `DIADRAW_GOOGLE_CLIENT_ID` and `DIADRAW_ONEDRIVE_CLIENT_ID`
  (**Settings › Secrets and variables › Actions › Variables**).
- **macOS, Linux and Windows apps:** pass them to CMake:

  ```bash
  cmake --preset release -DDIADRAW_GOOGLE_CLIENT_ID=… -DDIADRAW_GOOGLE_CLIENT_SECRET=… \
                         -DDIADRAW_ONEDRIVE_CLIENT_ID=…
  ```

  Environment variables with the same names override them when the app starts, which is handy
  for trying an app registration without rebuilding.
