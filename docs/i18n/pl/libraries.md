# Biblioteki kształtów

Biblioteka to nazwany, wersjonowany zestaw **kształtów** (rysunków), **kształtów złożonych**
(rysowanych z innych kształtów), **rodzajów elementów** (innych niż wbudowane w DSL) oraz
**fragmentów** (kodu źródłowego DiaDraw do wstawienia), uzupełniony o **mapowania** nazw
stosowanych w innych narzędziach. Biblioteki służą do rozszerzania DiaDraw: wtyczka jest
biblioteką. Dokument wskazuje używane biblioteki, a aplikacja, która go otwiera, je odnajduje.
Ani renderowanie, ani importowanie nie wymaga dostępu do sieci i nigdy z niej nie korzysta.

```
use "c4@^1"
use "aws-icons@^2.1" as aws

model {
  user = person "User" shape c4.person
  fn = component "Handler" tech "Node" shape aws.lambda
  user -> fn
}
```

- `use "<name>[@<range>]" [as <alias>]` umieszcza się przed modelem. Bez `as` aliasem jest
  ostatni człon nazwy, w którym pozostałe znaki zastępuje się znakiem `_` (`@acme/cloud-shapes` →
  `cloud_shapes`). Aliasy muszą być unikatowe w obrębie dokumentu.
- `shape <alias>.<shape>` wskazuje zwykły lub złożony kształt z biblioteki. Jeśli aplikacja
  nie może go odnaleźć, rysuje element z kształtem typowym dla jego rodzaju. `diadraw check`,
  `diadraw render` i edytor zgłaszają wtedy ostrzeżenie.
- `<id> = <alias>.<kind>` deklaruje element rodzaju dodawanego przez bibliotekę (zob.
  [Rodzaje elementów](#rodzaje-elementów)). Element nieznanego rodzaju jest rysowany jako
  prostokąt i powoduje analogiczne ostrzeżenie.
- Zakresy wersji są zgodne z semver: `^1`, `~1.2`, `1.x`, `>=1.2 <2`, `1.2 - 1.6`, `^1 || ^2`
  lub konkretna wersja.

## Wybór wersji biblioteki

Dla deklaracji `use` wybierana jest kolejno:

1. Wersja przypisana dokładnie temu `name@range` w pliku `diadraw-lock.json` (najbliższym
   takim pliku w katalogu dokumentu lub w którymś z katalogów nadrzędnych), jeśli jest dostępna
   w pamięci podręcznej.
2. Najwyższa wersja w pamięci podręcznej spełniająca wymagania zakresu.
3. Najwyższa wersja wbudowana spełniająca wymagania zakresu.

Lokalizację pamięci podręcznej określa `$DIADRAW_CACHE`. Jeśli nie ustawiono tej zmiennej,
używany jest katalog `$XDG_CACHE_HOME/diadraw/libraries`, a w ostateczności
`~/.cache/diadraw/libraries`. Każdej wersji odpowiada w nim jeden pakiet JSON. Na tym
etapie nic nie jest pobierane: biblioteka, której nie ma w żadnym z tych miejsc, jest
zgłaszana jako niezainstalowana. Aplikacja internetowa ma tylko biblioteki wbudowane.

## Polecenia

```bash
diadraw lib init shapes --name team-shapes   # manifest, an example shape and fragment, README
diadraw lib build shapes                     # check and build → shapes/dist/library.json
diadraw lib pack shapes                      # → shapes/dist/team-shapes-0.1.0.ddlib and its integrity
diadraw lib add shapes/dist/team-shapes-0.1.0.ddlib   # install into the cache, pin in diadraw-lock.json
diadraw lib add https://example.com/libs/aws-icons-2.1.0.ddlib
diadraw lib add c4@^1                        # pin a built-in (or cached) library
diadraw lib install                          # make the cache hold everything the lockfile pins
diadraw lib list                             # built-in and installed libraries
diadraw lib build --drawio stencils.min.js --set aws4,flowchart -o libs
```

Polecenie `add` przyjmuje katalog biblioteki, plik `.ddlib` lub pakiet `.json`, adres URL
`http(s)` albo specyfikację `name[@range]` biblioteki wbudowanej lub znajdującej się
w pamięci podręcznej. Wypisuje wiersz `use` do wklejenia. Tylko `add` i `install` korzystają
z sieci, i to wyłącznie w przypadku adresów URL przekazanych do `add` lub zapisanych w pliku
blokady. Dla każdego pakietu `install` sprawdza zgodność z wartością `integrity`
(`sha256-…` kanonicznej postaci JSON pakietu) i wersją przypisaną w pliku blokady.

Plik blokady `diadraw-lock.json` należy zapisać w repozytorium:

```json
{
  "lockfileVersion": 1,
  "libraries": {
    "c4@^1": { "version": "1.0.0", "resolved": "builtin", "integrity": "sha256-…" },
    "team-shapes@^0.1.0": {
      "version": "0.1.0",
      "resolved": "libs/team-shapes-0.1.0.ddlib",
      "integrity": "sha256-…"
    }
  }
}
```

Wartością `resolved` jest ścieżka względem pliku blokady, adres URL, `builtin` lub
`cache:<name>@<version>` w przypadku biblioteki, która istniała wyłącznie w lokalnej
pamięci podręcznej (`install` nie może jej odtworzyć w innym miejscu).

## Manifest

W katalogu źródłowym biblioteki znajduje się plik `diadraw-library.json`:

```json
{
  "name": "team-shapes",
  "version": "0.1.0",
  "description": "Shapes for our services",
  "license": "MIT",
  "source": { "kind": "original" },
  "shapes": {
    "gateway": {
      "label": "API gateway",
      "category": "Edge",
      "kind": "component",
      "svg": "shapes/gateway.svg",
      "width": 48,
      "height": 48,
      "keywords": ["api", "ingress"]
    },
    "store": { "label": "Store", "base": "cylinder", "kind": "database" }
  },
  "kinds": {
    "edge": {
      "label": "Edge service",
      "description": "Anything that terminates client traffic",
      "shape": "gateway",
      "stereotype": "edge"
    },
    "topic": { "label": "Topic", "base": "hexagon", "fill": "#fde68a", "stroke": "#b45309" }
  },
  "composites": {
    "gateway_with_store": {
      "width": 120,
      "height": 48,
      "parts": [
        { "shape": "gateway", "x": 0, "y": 0, "width": 48, "height": 48 },
        { "shape": "store", "x": 72, "y": 0, "width": 48, "height": 48 }
      ]
    }
  },
  "fragments": {
    "edge": {
      "label": "Gateway in front of a service",
      "params": [{ "name": "service", "default": "api" }],
      "text": "fragments/edge.draw"
    }
  },
  "mappings": { "drawio": { "mxgraph.team.gateway": "gateway" } },
  "themes": {
    "corporate": {
      "extends": "default",
      "background": "#fafafa",
      "edge": "#4a4a4a",
      "nodes": { "system": { "fill": "#aa0000", "stroke": "#7a0000", "text": "#ffffff" } }
    }
  },
  "dependencies": { "c4": "^1" }
}
```

| Pole | Znaczenie |
| --- | --- |
| `name`, `version` | Nazwa w stylu npm (małe litery, `-._`, opcjonalny prefiks `@scope/`), wersja semver. |
| `license` | Wyrażenie SPDX dotyczące rysunków; dodatkowe warunki podaje się w `licenseNotes`. |
| `source` | Pochodzenie rysunków: `original`, `drawio-stencils`, `plantuml-stdlib`, `svg`, `other`, z polami `url`, `commit`, `sets`. |
| `shapes` | Identyfikator → `svg` (ścieżka do pliku lub kod osadzony w manifeście) albo `base` (kształt podstawowy mechanizmu renderującego), `label`, `category`, `kind` dla nowych elementów, rozmiar, `keywords`, `style`. |
| `composites` | Kształt rysowany z części (`parts`); każda jest kształtem z tej biblioteki (lub `dep.shape` z zależności) umieszczonym w obrębie kształtu złożonego. |
| `kinds` | Identyfikator → rodzaj elementu: `shape` (zwykły lub złożony kształt z tej biblioteki albo `dep.shape`) lub `base`, `fill`, `stroke`, `fontColor`, `stereotype`, `label`, `description`, `category`, `keywords`. |
| `fragments` | Kod źródłowy DiaDraw (plik `.draw` lub kod osadzony w manifeście) wstawiany do `model { … }`; `${param}` oznacza wartość parametru, a `${alias}` — alias, pod którym używana jest biblioteka. |
| `open` | Akceptuje dowolną nazwę kształtu i rysuje go jako `base` (dla zestawów ikon, które nie wyliczają nazw). |
| `mappings` | Dla każdego formatu (`plantuml`, `plantuml-include`, `plantuml-local`, `structurizr`, `drawio`, `bpmn`): nazwa zewnętrzna → identyfikator kształtu. |
| `themes` | Identyfikator → motyw jak w `theme … { … }` ([dsl.md](dsl.md#własne-motywy)): `extends` (motyw tej biblioteki, motyw wbudowany lub `dep.theme`; motyw tej biblioteki ma pierwszeństwo przed wbudowanym o tej samej nazwie, chyba że motyw rozszerza własną nazwę), klucze motywu, `nodes`. |
| `dependencies` | Biblioteki, z których korzystają kształty złożone: `name` → zakres wersji. |

Pole `kind` zwykłego lub złożonego kształtu wskazuje rodzaj elementu dodawanego z palety.
Jeśli biblioteka definiuje własny rodzaj o tej nazwie, używany jest właśnie on; w przeciwnym
razie wybierany jest rodzaj wbudowany.

## Rodzaje elementów

DSL ma kilka wbudowanych rodzajów elementów (`person`, `system`, `container`, `component`,
`database`, `process`, `decision`, `start`, `end`, `note` oraz rodzaje UML i BPMN używane
przez importery). Biblioteka może dodać kolejne w sekcji `kinds`. W dokumencie odwołuje
się do nich przez alias:

```
use "team-shapes@^0.1" as team

model {
  lb = team.edge "Load balancer"
  orders = team.topic "Orders"
  lb -> orders
}
```

Rodzaj określa wygląd elementów. Wszystko inne (etykiety, `tech`, elementy potomne, porty,
relacje, widoki) działa tak samo jak w przypadku pozostałych elementów.

- **Kształt.** `shape` rysuje element za pomocą rysunku z biblioteki (rozmiar i etykietę
  ustala tak jak dla odwołania `shape`); `base` używa jednego z kształtów podstawowych
  mechanizmu renderującego (`hexagon`, `cylinder`, …). Pierwszeństwo ma `shape` samego elementu.
- **Kolory.** `fill`, `stroke` i `fontColor` zastępują domyślne kolory węzłów z motywu dla
  danego rodzaju. Pierwszeństwo ma własny `style` elementu, a elementy `external` zachowują
  kolory elementów zewnętrznych.
- **Stereotyp.** Element danego rodzaju, który ma elementy potomne, jest rysowany jako
  obramowanie z etykietą `Name [stereotype]`.
- **Dostępność w narzędziach.** Serwer językowy podpowiada po `=` rodzaje z przedrostkiem
  `alias.` i wskazuje ostrzeżeniem nazwę rodzaju, którego nie ma w bibliotece. Tak samo
  ostrzegają `diadraw check` i `render`. Paleta w aplikacji internetowej wyświetla rodzaje
  w sekcji *Kinds*; wybranie jednego z nich deklaruje element tego rodzaju.

W modelu rodzaje zachowują pełną nazwę z aliasem (`team.topic`), dzięki czemu pozostaje ona
również w dokumencie po zapisaniu modelu w postaci tekstu.

Polecenie `lib build` wczytuje pliki SVG i oczyszcza je (usuwa skrypty, procedury obsługi
zdarzeń, obiekty obce i odwołania zewnętrzne; do identyfikatorów dodaje przedrostki zależne
od kształtu, by ikony na jednej stronie nie kolidowały ze sobą). Następnie osadza całość
w jednym pakiecie (`format: "diadraw-library"`, `formatVersion: 1`) i sprawdza, czy każdy
fragment przechodzi analizę składniową. Plik `.ddlib` to ten sam pakiet skompresowany gzipem.

## Licencje

Każda biblioteka ma licencję. Sprawdzają ją `lib build`, `lib add` oraz generator bibliotek
wbudowanych:

| Status | Kiedy | Skutek |
| --- | --- | --- |
| `allowed` | Wszystkie składniki co najmniej jednego wariantu licencji są na liście dozwolonych (MIT, MIT-0, Apache-2.0, BSD-2/3-Clause, ISC, 0BSD, CC0-1.0, CC-BY-4.0, Unlicense) i nie ma dodatkowych warunków; lub jest to oryginalne dzieło tego projektu. | Bibliotekę można rozpowszechniać z DiaDraw. |
| `restricted` | Dozwolone licencje z `licenseNotes`. | Ostrzeżenie; biblioteki można używać lokalnie, ale nie jest dołączana do dystrybucji. |
| `disallowed` | W każdym wariancie występuje zakaz użytku komercyjnego lub tworzenia utworów zależnych albo licencja typu copyleft (CC-BY-NC/ND, GPL, AGPL, LGPL, SSPL, BUSL). | `lib build` kończy się błędem. |
| `unknown` | Pozostałe przypadki. | Ostrzeżenie. |

Do bibliotek wbudowanych trafiają z `assets/builtin/` wyłącznie biblioteki o statusie
`allowed`. Jedyny wyjątek to zestawy szablonów draw.io, które decyzją projektu są dołączane
mimo statusu `restricted` (zob. poniżej oraz `docs/decisions.md`).

## Biblioteki wbudowane

Źródła znajdują się w `assets/builtin/`. Polecenie `node tools/gen/builtin_libraries.mjs`
(wykonywane po `npm run build -w @diadraw/wasm`) buduje je za pomocą rdzenia i zapisuje
`src/library/src/builtin_libraries.inc`, który jest następnie wkompilowywany w rdzeń.

- **`c4`**: osoba, system oprogramowania, kontener, komponent, baza danych, kolejka,
  przeglądarka i aplikacja mobilna; każdy kształt ma też wariant zewnętrzny, o ile taki
  istnieje w C4. Ponadto kształt złożony „usługa z bazą danych” oraz fragmenty „kontekst
  systemu” i „aplikacja internetowa, API i baza danych”. Mapowania obejmują makra C4-PlantUML
  (`Person`, `System_Ext`, `ContainerDb`, `ComponentQueue`, …) oraz kształty Structurizr
  (`WebBrowser`, `Cylinder`, `Pipe`, …). Grafiki są oryginalne.
- **`drawio-*`**: 204 zestawy szablonów draw.io z ograniczeniami licencyjnymi; zob.
  [Szablony draw.io](#szablony-drawio).
- **`plantuml-aws`, `plantuml-azure`, `plantuml-gcp`, `plantuml-kubernetes`,
  `plantuml-tupadr3`, `plantuml-office`, `plantuml-cloudinsight`, `plantuml-logos`,
  `plantuml-material`, `plantuml-elastic`, `plantuml-osa`, `plantuml-cloudogu`, `plantuml-ibm`,
  `plantuml-archimate`**: zestawy ikon biblioteki standardowej PlantUML w postaci bibliotek
  otwartych na dowolne nazwy kształtów. Dzięki mapowaniom `plantuml-include` i `plantuml-local`
  importer PlantUML rozpoznaje `!include <awslib/…>`, typowe adresy URL i kopie dołączone do
  projektu. Makro ikony, np. `Lambda(fn, "Handler")`, jest importowane jako `shape aws.Lambda`
  z deklaracją `use "plantuml-aws@^1" as aws`. Samych ikon nie dołączono (mają różne licencje),
  więc kształty te są rysowane jako prostokątne pola ikon.
- **`bpmn`**: kształty BPMN 2.0 z oryginalnymi grafikami. Obejmują każdą pozycję zdarzenia
  (początkowe, początkowe nieprzerywające, początkowe podprocesu zdarzeniowego, pośrednie
  przechwytujące, krawędziowe nieprzerywające, pośrednie generujące, końcowe) z każdym
  znacznikiem dozwolonym w danym miejscu przez BPMN; zadania każdego typu, czynności wywołania,
  podprocesy, podprocesy zdarzeniowe, transakcje i podprocesy ad hoc — każde bez znacznika
  oraz ze znacznikiem pętli, wielu instancji równoległych lub sekwencyjnych bądź kompensacji;
  wszystkie bramki; obiekty, kolekcje, wejścia, wyjścia i magazyny danych; komunikaty,
  adnotacje tekstowe, grupy, konwersacje i zadania choreografii. Kształty czynności mają
  etykietę wewnątrz. Biblioteka zawiera też kształt złożony „zadanie z czasomierzem” oraz
  fragmenty „pętla zatwierdzania” i „zadanie z limitem czasu”. Mapowanie `bpmn` przypisuje
  kształt każdemu typowi elementu BPMN. Bibliotekę generuje `tools/gen/bpmn_library.mjs`.

## Szablony draw.io

Wszystkie zestawy szablonów draw.io są dostarczane jako biblioteki wbudowane o nazwach
`drawio-<set>` (`drawio-aws4`, `drawio-azure`, `drawio-cisco19`, `drawio-flowchart`, …).
Łącznie to 204 zestawy i 9088 kształtów z draw.io 32.6.9. Używa się ich jak każdej innej
biblioteki:

```
use "drawio-aws4@^32" as aws4

model {
  fn = component "Handler" shape aws4.lambda_function
}
```

**Obowiązują ograniczenia licencyjne.** Samo draw.io jest objęte licencją Apache-2.0, jednak
bez pisemnej zgody jego szablonów nie wolno używać w produktach ani na platformie handlowej
Atlassian, rozpowszechniać w celu użycia z nimi ani w nie włączać. Zestawy dostawców
(AWS, Azure, GCP, Cisco, IBM, …) podlegają ponadto ich warunkom dotyczącym znaków towarowych.
Zob. `assets/NOTICE`. Zarówno `diadraw lib list`, jak i paleta aplikacji internetowej oznaczają
każdy zestaw jako objęty ograniczeniami.

Zestawy są przechowywane w `assets/drawio/` w postaci skompresowanej gzipem i ładowane
na żądanie. Dopiero gdy dokument korzysta z zestawu lub zostanie on otwarty w palecie,
aplikacja odczytuje go (aplikacje natywne i środowiska WebAssembly, którym udostępniono
system plików za pomocą `setFileSystem(fs, { drawio })`) lub pobiera i dodaje przez
`addLibrary` (aplikacja internetowa).

Importer draw.io rysuje kształty z szablonów za pomocą tych bibliotek, w kolorach
wypełnienia i obrysu komórki (dotyczy to także kafelków z ikonami zasobów). Konwersja
odwzorowuje mxStencil: ścieżki, prostokąty, prostokąty z zaokrąglonymi rogami i elipsy
z obsługą wypełnienia, obrysu, kolorów, linii kreskowanych, przezroczystości oraz
zapisywania i przywracania stanu. Tekst, obrazy i zagnieżdżone szablony nie są rysowane,
a kształty rysowane przez kod draw.io nie są szablonami.

Ponowne generowanie na podstawie ustalonej wersji (`DRAWIO` w skrypcie):

```bash
npm run build -w @diadraw/wasm
node tools/gen/drawio_stencils.mjs                   # downloads the pinned files
node tools/gen/drawio_stencils.mjs --from ../drawio  # or a local checkout
```

Polecenie `diadraw lib build --drawio <stencils.min.js|set.xml> -o <dir>` nadal umożliwia
konwersję innych wersji lub pojedynczych zestawów na osobne biblioteki.

## Gdzie są dostępne biblioteki

- **Wiersz poleceń**: `render` i `check` odnajdują biblioteki dokumentu; `render` rysuje
  kształty biblioteczne.
- **Serwer językowy**: podpowiada nazwy bibliotek w `use "…"`, kształty po `shape`
  (wszystkie, jeśli jest ich najwyżej 400, w przeciwnym razie najpierw aliasy) i po
  przedrostkach `alias.`, a po `=` — rodzaje elementów z bibliotek. Wewnątrz `model { … }`
  proponuje wstawianie fragmentów kodu z polami do wypełnienia w miejscu parametrów.
  Brak biblioteki, nieznany kształt lub nieznany rodzaj elementu powodują ostrzeżenie.
- **Edytor internetowy**: przycisk **Shapes** otwiera paletę zwykłych i złożonych kształtów,
  rodzajów elementów i fragmentów z bibliotek wbudowanych, z wyszukiwaniem według nazwy
  i słów kluczowych. Kliknięcie kształtu dodaje element o tym kształcie (oraz deklarację
  `use` biblioteki, jeśli dokument jeszcze jej nie ma) jako jedną operację, którą można
  cofnąć. Kliknięcie fragmentu wstawia jego tekst na końcu modelu.
- **Motywy**: `theme alias.name` wybiera motyw biblioteczny; za jego odszukanie odpowiada
  `RenderOptions.themes` (`LibrarySet::theme_resolver()`). Wiersz poleceń i serwer językowy
  ostrzegają, jeśli biblioteka nie ma wskazanego motywu.
- **Mechanizm renderujący**: `RenderOptions.shapes` odnajduje rysunki wskazane przez
  odwołania; etykiety są umieszczane pod rysunkami. `RenderOptions.kinds` przypisuje
  rodzajom elementów z bibliotek kształt i kolory. `create_measure(theme, shapes, kinds)`
  odpowiednio dobiera ich rozmiary. `LibrarySet::renderer()` udostępnia oba mechanizmy.
