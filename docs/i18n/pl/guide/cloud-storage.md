# Google Drive i OneDrive

DiaDraw otwiera i zapisuje diagramy w Google Drive i OneDrive we wszystkich aplikacjach: w aplikacji webowej oraz w aplikacjach na macOS, Linuksa i Windows. Diagram zapisany w jednej aplikacji otwiera się w pozostałych, bo wszystkie korzystają z tego samego miejsca w każdej usłudze.

## Jak z tego korzystać

- **Aplikacja webowa:** przycisk **Storage…** na pasku narzędzi otwiera okno z kartą dla każdego miejsca, w którym może leżeć diagram: **This browser** (pamięć samej przeglądarki, jak dotąd), **Google Drive** i **OneDrive**.
- **macOS, Linux i Windows:** polecenie **File › Cloud Storage…** otwiera to samo okno z kartami Google Drive i OneDrive (te aplikacje trzymają diagramy w plikach, więc nie mają karty przeglądarki).

Na karcie usługi w chmurze przycisk **Sign in** otwiera stronę logowania tej usługi (w aplikacji webowej w wyskakującym oknie, w aplikacjach desktopowych w przeglądarce). Po zalogowaniu karta wyświetla Twoje diagramy w tej usłudze, od najnowszego. Z listy możesz:

- **Otworzyć** diagram (**Open**). W aplikacji webowej zastępuje on edytowany diagram; w aplikacjach desktopowych otwiera się w nowym oknie, chyba że bieżące okno to nietknięty nowy dokument.
- **Zapisać bieżący diagram** pod nową nazwą (**Save current diagram**). Od tej chwili ⌘S / Ctrl+S zapisuje go z powrotem do tego samego pliku w tej usłudze, a tytuł pokazuje, gdzie się znajduje, np. „Checkout (OneDrive)”.
- **Zmienić nazwę** diagramu (**Rename**) i go **usunąć** (**Delete**). Usunięcie przenosi go do kosza Google Drive lub OneDrive, skąd można go przywrócić.

Jeśli w chwili zapisu logowanie wygasło, okno otwiera się na tej usłudze i prosi o ponowne zalogowanie; potem zapisz jeszcze raz.

## Gdzie trafiają diagramy i co widzi DiaDraw

DiaDraw prosi każdą usługę o najmniejszy dostęp, jaki ona oferuje:

| Usługa       | Dostęp, o który prosi DiaDraw                                           | Gdzie zapisywane są diagramy                   |
| ------------ | ----------------------------------------------------------------------- | ---------------------------------------------- |
| Google Drive | `drive.file`: tylko pliki, które DiaDraw sam utworzył lub otworzył      | `<nazwa>.draw` w katalogu głównym Mojego dysku |
| OneDrive     | `Files.ReadWrite.AppFolder`: tylko własny folder DiaDraw na Twoim dysku | `<nazwa>.draw` w folderze **Apps › DiaDraw**   |

DiaDraw nie może więc czytać Twoich innych plików. W OneDrive pliki `.draw`, które sam skopiujesz do **Apps › DiaDraw**, też pojawią się w DiaDraw; w Google Drive widać tylko pliki zapisane przez DiaDraw.

Aplikacje desktopowe pamiętają logowanie, dopóki nie zostaną zamknięte. Aplikacja webowa przechowuje je dla karty przeglądarki (w jej pamięci sesji), więc po odświeżeniu strony pozostajesz zalogowany, a po zamknięciu karty logowanie znika.

## Konfiguracja dla własnej kompilacji DiaDraw

Każda usługa rozmawia tylko z aplikacjami u niej zarejestrowanymi, więc ten, kto kompiluje i wydaje DiaDraw, rejestruje go raz w Google i w Microsoft i wbudowuje identyfikatory w kompilację. Kompilacja bez nich pokazuje kartę z napisem „not set up” zamiast przycisku Sign in. Identyfikatory nie są tajne.

### Google Drive

W [konsoli Google Cloud](https://console.cloud.google.com/):

1. Utwórz projekt (na przykład „DiaDraw”).
2. **APIs & Services › Library:** włącz **Google Drive API**.
3. **Google Auth Platform › Branding:** nazwa aplikacji, adres pomocy i strona główna (adres witryny). **Audience:** External; dopóki aplikacja jest w trybie „Testing”, zalogować się mogą tylko wymienieni tam użytkownicy testowi, więc opublikuj ją, gdy ma działać dla wszystkich.
4. **Data Access:** dodaj zakres `https://www.googleapis.com/auth/drive.file`. To zakres niewrażliwy, więc aplikacja nie potrzebuje przeglądu bezpieczeństwa Google.
5. **Clients › Create client › Web application** (dla aplikacji webowej):
   - Authorised JavaScript origins: origin witryny, np. `https://mykulyak.github.io`, oraz `http://localhost:5173` dla `npm run dev:web`.
   - Authorised redirect URIs: plik `oauth.html` aplikacji, np. `https://mykulyak.github.io/diadraw/app/oauth.html`, oraz `http://localhost:5173/oauth.html`.
   - Jego **client ID** to `VITE_GOOGLE_CLIENT_ID` poniżej.
6. **Clients › Create client › Desktop app** (dla aplikacji na macOS, Linuksa i Windows). Jego **client ID** i **client secret** to `DIADRAW_GOOGLE_CLIENT_ID` i `DIADRAW_GOOGLE_CLIENT_SECRET` poniżej. (Google wydaje sekret nawet dla aplikacji desktopowych; trafia on do każdej kopii, więc nie jest tajny, a logowanie chroni zamiast niego PKCE).

### OneDrive

W [centrum administracyjnym Microsoft Entra](https://entra.microsoft.com/), **App registrations › New registration**:

1. Nazwa: „DiaDraw”. Supported account types: **Accounts in any organizational directory and personal Microsoft accounts**.
2. **Authentication › Add a platform › Single-page application** (dla aplikacji webowej), z adresami przekierowania `https://mykulyak.github.io/diadraw/app/oauth.html` i `http://localhost:5173/oauth.html`.
3. **Authentication › Add a platform › Mobile and desktop applications** (dla aplikacji desktopowych), z własnym adresem przekierowania `http://localhost`. (Na `localhost` akceptowany jest dowolny port).
4. **API permissions › Add a permission › Microsoft Graph › Delegated:** `Files.ReadWrite.AppFolder` i `offline_access`.
5. **Application (client) ID** ze strony przeglądu to jednocześnie `VITE_ONEDRIVE_CLIENT_ID` i `DIADRAW_ONEDRIVE_CLIENT_ID` poniżej. Sekretu nie ma.

### Wbudowanie identyfikatorów

- **Aplikacja webowa:** ustaw `VITE_GOOGLE_CLIENT_ID` i `VITE_ONEDRIVE_CLIENT_ID` w środowisku kompilacji (lub w `src/apps/web/.env.local` dla `npm run dev:web`). Workflow GitHub Pages bierze je ze zmiennych repozytorium `DIADRAW_GOOGLE_CLIENT_ID` i `DIADRAW_ONEDRIVE_CLIENT_ID` (**Settings › Secrets and variables › Actions › Variables**).
- **Aplikacje na macOS, Linuksa i Windows:** przekaż je do CMake:

  ```bash
  cmake --preset release -DDIADRAW_GOOGLE_CLIENT_ID=… -DDIADRAW_GOOGLE_CLIENT_SECRET=… \
                         -DDIADRAW_ONEDRIVE_CLIENT_ID=…
  ```

  Zmienne środowiskowe o tych samych nazwach zastępują je przy starcie aplikacji, co przydaje się do wypróbowania rejestracji aplikacji bez ponownej kompilacji.
