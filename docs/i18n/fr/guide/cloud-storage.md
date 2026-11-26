# Google Drive et OneDrive

DiaDraw ouvre et enregistre des diagrammes dans Google Drive et OneDrive, dans toutes ses applications : l’application web et les applications macOS, Linux et Windows. Un diagramme enregistré depuis une application s’ouvre dans les autres, car toutes utilisent le même emplacement dans chaque service.

## Utilisation

- **Application web :** le bouton **Storage…** de la barre d’outils ouvre une boîte de dialogue avec un onglet pour chaque endroit où un diagramme peut se trouver : **This browser** (le stockage du navigateur lui-même, comme avant), **Google Drive** et **OneDrive**.
- **macOS, Linux et Windows :** **File › Cloud Storage…** ouvre la même boîte de dialogue avec les onglets Google Drive et OneDrive (ces applications conservent les diagrammes dans des fichiers et n’ont donc pas d’onglet pour le navigateur).

Dans un onglet de service en ligne, **Sign in** ouvre la page de connexion du service (une fenêtre surgissante dans l’application web, votre navigateur dans les applications de bureau). Une fois connecté, l’onglet liste vos diagrammes dans ce service, du plus récent au plus ancien. Depuis la liste, vous pouvez :

- **Ouvrir** un diagramme (**Open**). Dans l’application web, il remplace celui en cours d’édition ; dans les applications de bureau, il s’ouvre dans une nouvelle fenêtre, sauf si la fenêtre actuelle est un nouveau document encore intact.
- **Enregistrer le diagramme courant** sous un nouveau nom (**Save current diagram**). Dès lors, ⌘S / Ctrl+S l’enregistre dans le même fichier de ce service, et le titre indique où il se trouve, par exemple « Checkout (OneDrive) ».
- **Renommer** (**Rename**) et **supprimer** (**Delete**) un diagramme. La suppression le place dans la corbeille de Google Drive ou de OneDrive, d’où il peut être restauré.

Si la connexion a expiré au moment d’enregistrer, la boîte de dialogue s’ouvre sur ce service et vous demande de vous reconnecter ; enregistrez ensuite à nouveau.

## Où vont les diagrammes, et ce que DiaDraw peut voir

DiaDraw demande à chaque service le moins d’accès possible :

| Service      | Accès demandé par DiaDraw                                                              | Emplacement des diagrammes                      |
| ------------ | -------------------------------------------------------------------------------------- | ----------------------------------------------- |
| Google Drive | `drive.file` : uniquement les fichiers que DiaDraw a lui-même créés ou ouverts         | `<nom>.draw` à la racine de Mon Drive           |
| OneDrive     | `Files.ReadWrite.AppFolder` : uniquement le dossier propre à DiaDraw dans votre espace | `<nom>.draw` dans le dossier **Apps › DiaDraw** |

DiaDraw ne peut donc pas lire vos autres fichiers. Dans OneDrive, les fichiers `.draw` que vous copiez vous-même dans **Apps › DiaDraw** apparaissent aussi dans DiaDraw ; dans Google Drive, seuls les fichiers enregistrés par DiaDraw apparaissent.

Les applications de bureau gardent la connexion en mémoire jusqu’à ce qu’elles soient quittées. L’application web la conserve pour l’onglet du navigateur (dans son stockage de session) : un rechargement garde la connexion, et la fermeture de l’onglet l’oublie.

## Configuration pour une version de DiaDraw

Chaque service ne communique qu’avec les applications enregistrées auprès de lui ; la personne qui compile et distribue DiaDraw l’enregistre donc une fois auprès de Google et de Microsoft et intègre les identifiants à la compilation. Une version sans eux affiche l’onglet avec « not set up » à la place du bouton Sign in. Les identifiants ne sont pas secrets.

### Google Drive

Dans la [console Google Cloud](https://console.cloud.google.com/) :

1. Créez un projet (par exemple « DiaDraw »).
2. **APIs & Services › Library :** activez la **Google Drive API**.
3. **Google Auth Platform › Branding :** le nom de l’application, une adresse d’assistance et la page d’accueil (l’adresse du site). **Audience :** External ; tant que l’application est en mode « Testing », seuls les utilisateurs de test qui y sont listés peuvent se connecter, publiez-la donc quand elle doit fonctionner pour tout le monde.
4. **Data Access :** ajoutez le champ d’application `https://www.googleapis.com/auth/drive.file`. Ce champ n’est pas sensible : l’application n’a pas besoin de l’examen de sécurité de Google.
5. **Clients › Create client › Web application** (pour l’application web) :
   - Authorised JavaScript origins : l’origine du site, par exemple `https://mykulyak.github.io`, et `http://localhost:5173` pour `npm run dev:web`.
   - Authorised redirect URIs : le fichier `oauth.html` de l’application, par exemple `https://mykulyak.github.io/diadraw/app/oauth.html`, et `http://localhost:5173/oauth.html`.
   - Son **client ID** est `VITE_GOOGLE_CLIENT_ID` ci-dessous.
6. **Clients › Create client › Desktop app** (pour les applications macOS, Linux et Windows). Son **client ID** et son **client secret** sont `DIADRAW_GOOGLE_CLIENT_ID` et `DIADRAW_GOOGLE_CLIENT_SECRET` ci-dessous. (Google délivre un secret même pour les applications de bureau ; il est livré dans chaque copie et n’est donc pas secret : c’est PKCE qui protège la connexion.)

### OneDrive

Dans le [centre d’administration Microsoft Entra](https://entra.microsoft.com/), **App registrations › New registration** :

1. Nom : « DiaDraw ». Supported account types : **Accounts in any organizational directory and personal Microsoft accounts**.
2. **Authentication › Add a platform › Single-page application** (pour l’application web), avec les URI de redirection `https://mykulyak.github.io/diadraw/app/oauth.html` et `http://localhost:5173/oauth.html`.
3. **Authentication › Add a platform › Mobile and desktop applications** (pour les applications de bureau), avec l’URI de redirection personnalisée `http://localhost`. (Tout port est accepté sur `localhost`.)
4. **API permissions › Add a permission › Microsoft Graph › Delegated :** `Files.ReadWrite.AppFolder` et `offline_access`.
5. L’**Application (client) ID** de la page de présentation est à la fois `VITE_ONEDRIVE_CLIENT_ID` et `DIADRAW_ONEDRIVE_CLIENT_ID` ci-dessous. Il n’y a pas de secret.

### Intégrer les identifiants

- **Application web :** définissez `VITE_GOOGLE_CLIENT_ID` et `VITE_ONEDRIVE_CLIENT_ID` dans l’environnement de compilation (ou dans `src/apps/web/.env.local` pour `npm run dev:web`). Le workflow GitHub Pages les prend dans les variables du dépôt `DIADRAW_GOOGLE_CLIENT_ID` et `DIADRAW_ONEDRIVE_CLIENT_ID` (**Settings › Secrets and variables › Actions › Variables**).
- **Applications macOS, Linux et Windows :** passez-les à CMake :

  ```bash
  cmake --preset release -DDIADRAW_GOOGLE_CLIENT_ID=… -DDIADRAW_GOOGLE_CLIENT_SECRET=… \
                         -DDIADRAW_ONEDRIVE_CLIENT_ID=…
  ```

  Les variables d’environnement de même nom les remplacent au démarrage de l’application, ce qui est pratique pour essayer un enregistrement d’application sans recompiler.
