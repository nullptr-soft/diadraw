# Google Drive und OneDrive

DiaDraw öffnet und speichert Diagramme in Google Drive und OneDrive, und zwar in jeder App: in der Web-App sowie in den Apps für macOS, Linux und Windows. Ein in einer App gespeichertes Diagramm lässt sich in den anderen öffnen, weil alle denselben Ort in jedem Dienst verwenden.

## So benutzt du es

- **Web-App:** Die Schaltfläche **Storage…** in der Werkzeugleiste öffnet einen Dialog mit einem Tab für jeden Ort, an dem ein Diagramm liegen kann: **This browser** (der Speicher des Browsers selbst, wie bisher), **Google Drive** und **OneDrive**.
- **macOS, Linux und Windows:** **File › Cloud Storage…** öffnet denselben Dialog mit den Tabs Google Drive und OneDrive (diese Apps speichern Diagramme in Dateien und haben daher keinen Browser-Tab).

In einem Cloud-Tab öffnet **Sign in** die Anmeldeseite des Dienstes (in der Web-App als Pop-up, in den Desktop-Apps in deinem Browser). Nach der Anmeldung listet der Tab deine Diagramme in diesem Dienst auf, das neueste zuerst. In der Liste kannst du:

- ein Diagramm **öffnen** (**Open**). In der Web-App ersetzt es das gerade bearbeitete; in den Desktop-Apps öffnet es sich in einem neuen Fenster, es sei denn, das aktuelle Fenster ist ein unberührtes neues Dokument.
- das **aktuelle Diagramm** unter einem neuen Namen **speichern** (**Save current diagram**). Ab dann speichert ⌘S / Strg+S zurück in dieselbe Datei in diesem Dienst, und der Titel zeigt, wo es liegt, etwa „Checkout (OneDrive)“.
- ein Diagramm **umbenennen** (**Rename**) und **löschen** (**Delete**). Beim Löschen landet es im Papierkorb von Google Drive oder OneDrive, wo es wiederhergestellt werden kann.

Ist beim Speichern eine Anmeldung abgelaufen, öffnet sich der Dialog bei diesem Dienst und bittet dich, dich erneut anzumelden; speichere danach noch einmal.

## Wohin Diagramme gespeichert werden und was DiaDraw sieht

DiaDraw fordert bei jedem Dienst den geringsten Zugriff an, den er anbietet:

| Dienst       | Zugriff, den DiaDraw anfordert                                              | Speicherort der Diagramme                            |
| ------------ | --------------------------------------------------------------------------- | ---------------------------------------------------- |
| Google Drive | `drive.file`: nur Dateien, die DiaDraw selbst erstellt oder geöffnet hat    | `<name>.draw` im Stammverzeichnis von „Meine Ablage“ |
| OneDrive     | `Files.ReadWrite.AppFolder`: nur DiaDraws eigener Ordner in deinem Speicher | `<name>.draw` im Ordner **Apps › DiaDraw**           |

DiaDraw kann deine anderen Dateien also nicht lesen. In OneDrive erscheinen auch `.draw`-Dateien, die du selbst nach **Apps › DiaDraw** kopierst, in DiaDraw; in Google Drive nur die Dateien, die DiaDraw gespeichert hat.

Die Desktop-Apps behalten eine Anmeldung im Speicher, bis sie beendet werden. Die Web-App behält sie für den Browser-Tab (in dessen Sitzungsspeicher), sodass du nach einem Neuladen angemeldet bleibst; beim Schließen des Tabs wird sie vergessen.

## Einrichtung für einen eigenen DiaDraw-Build

Jeder Dienst spricht nur mit Apps, die bei ihm registriert sind. Wer DiaDraw baut und ausliefert, registriert es daher einmal bei Google und bei Microsoft und baut die IDs ein. Ein Build ohne sie zeigt im Tab „not set up“ statt der Schaltfläche Sign in. Die IDs sind keine Geheimnisse.

### Google Drive

In der [Google Cloud Console](https://console.cloud.google.com/):

1. Lege ein Projekt an (zum Beispiel „DiaDraw“).
2. **APIs & Services › Library:** Aktiviere die **Google Drive API**.
3. **Google Auth Platform › Branding:** App-Name, eine Support-Adresse und die Startseite (die Adresse der Website). **Audience:** External; solange die App im Status „Testing“ ist, können sich nur die dort eingetragenen Testnutzer anmelden. Veröffentliche sie also, wenn sie für alle funktionieren soll.
4. **Data Access:** Füge den Bereich `https://www.googleapis.com/auth/drive.file` hinzu. Er ist nicht sensibel, daher braucht die App keine Sicherheitsprüfung durch Google.
5. **Clients › Create client › Web application** (für die Web-App):
   - Authorised JavaScript origins: der Origin der Website, z. B. `https://mykulyak.github.io`, und `http://localhost:5173` für `npm run dev:web`.
   - Authorised redirect URIs: die `oauth.html` der App, z. B. `https://mykulyak.github.io/diadraw/app/oauth.html`, und `http://localhost:5173/oauth.html`.
   - Seine **client ID** ist `VITE_GOOGLE_CLIENT_ID` unten.
6. **Clients › Create client › Desktop app** (für die Apps für macOS, Linux und Windows). Seine **client ID** und sein **client secret** sind `DIADRAW_GOOGLE_CLIENT_ID` und `DIADRAW_GOOGLE_CLIENT_SECRET` unten. (Google vergibt auch für Desktop-Apps ein Secret; es steckt in jeder Kopie und ist daher nicht geheim, die Anmeldung schützt stattdessen PKCE.)

### OneDrive

Im [Microsoft Entra Admin Center](https://entra.microsoft.com/), **App registrations › New registration**:

1. Name: „DiaDraw“. Supported account types: **Accounts in any organizational directory and personal Microsoft accounts**.
2. **Authentication › Add a platform › Single-page application** (für die Web-App), mit den Umleitungs-URIs `https://mykulyak.github.io/diadraw/app/oauth.html` und `http://localhost:5173/oauth.html`.
3. **Authentication › Add a platform › Mobile and desktop applications** (für die Desktop-Apps), mit der benutzerdefinierten Umleitungs-URI `http://localhost`. (Auf `localhost` wird jeder Port akzeptiert.)
4. **API permissions › Add a permission › Microsoft Graph › Delegated:** `Files.ReadWrite.AppFolder` und `offline_access`.
5. Die **Application (client) ID** auf der Übersichtsseite ist sowohl `VITE_ONEDRIVE_CLIENT_ID` als auch `DIADRAW_ONEDRIVE_CLIENT_ID` unten. Ein Secret gibt es nicht.

### Die IDs einbauen

- **Web-App:** Setze `VITE_GOOGLE_CLIENT_ID` und `VITE_ONEDRIVE_CLIENT_ID` in der Umgebung des Builds (oder in `src/apps/web/.env.local` für `npm run dev:web`). Der GitHub-Pages-Workflow nimmt sie aus den Repository-Variablen `DIADRAW_GOOGLE_CLIENT_ID` und `DIADRAW_ONEDRIVE_CLIENT_ID` (**Settings › Secrets and variables › Actions › Variables**).
- **Apps für macOS, Linux und Windows:** Übergib sie an CMake:

  ```bash
  cmake --preset release -DDIADRAW_GOOGLE_CLIENT_ID=… -DDIADRAW_GOOGLE_CLIENT_SECRET=… \
                         -DDIADRAW_ONEDRIVE_CLIENT_ID=…
  ```

  Umgebungsvariablen mit denselben Namen überschreiben sie beim Start der App, was praktisch ist, um eine App-Registrierung ohne Neubau auszuprobieren.
