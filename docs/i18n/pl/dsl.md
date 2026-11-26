# Język DSL DiaDraw

Jedna gramatyka dla wszystkich typów diagramów: w bloku `model`, wzorowanym na Structurizr, definiujesz elementy i relacje tylko raz, a w blokach `view` wybierasz je, rozmieszczasz i określasz ograniczenia. Pliki z rozszerzeniem `.draw` są źródłem prawdy, dzięki czemu w Git łatwo porównywać wersje diagramów i przeglądać zmiany.

Gramatykę opisuje plik [`diadraw.grammar`](diadraw.grammar). Zapis w notacji Lezer służy jako specyfikacja; żaden program go nie kompiluje. Parser rdzenia (`src/dsl/src/syntax.cpp`) to napisany ręcznie, odporny na błędy parser z rekurencyjnym zstępowaniem. Buduje drzewo opisane przez gramatykę, z takimi samymi nazwami węzłów. Funkcja `parse(source)` (`src/dsl/src/lower.cpp`) przekształca drzewo składniowe w operacje na modelu i zwraca model, komunikaty diagnostyczne z numerami wierszy i kolumn oraz mapę źródła. Funkcja `print(model)` zapisuje tekst w postaci kanonicznej; dla każdego pliku z korpusu zapis i ponowne parsowanie dają niezmieniony model.

Ten dokument opisuje język. Do czego służą poszczególne funkcje i kiedy ich używać, wyjaśnia [podręcznik użytkownika](guide/README.md).

## Przykład

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
  pin shop.db below shop.web    // written by a canvas drag
}
```

## Diagramy swobodne

Diagramy rysowane ręcznie (lub importowane z draw.io) zachowują swoją geometrię w tekście:

```
model {
  a = process "Billing" style "fill=#dae8fc;stroke=#6c8ebf;rounded=1" prop "owner" "Ann"
  b = decision "Paid?" shape rhombus
  a -> b "check" style "endArrow=open;dashed=1;edgeStyle=orthogonalEdgeStyle"
}

view page1 {
  include *
  layout fixed
  pin a at 40, 40 size 120, 60
  pin b at 240, 30 size 100, 80
  route "a->b" via 200, 70 200, 70 from 1, 0.5 to 0, 0.5
}
```

- `style "k=v;k=v"` określa wygląd elementu lub relacji. Klucze elementu: `fill`, `stroke`, `strokeWidth`, `dashed`, `dashPattern`, `rounded`, `opacity` (0–100), `fontColor`, `fontSize`, `fontStyle` (pogrubienie 1, kursywa 2, podkreślenie 4), `align`, `verticalAlign`, `labelPosition`, `verticalLabelPosition`. Klucze relacji: `stroke`, `strokeWidth`, `dashed`, `dashPattern` (przy `dashed=1`: `1 3` oznacza linię kropkowaną, `8 3 1 3` — kreskowo-kropkową itd.), `double` (dwie linie), `curved`, `rounded` (zaokrąglone narożniki), `edgeStyle`, `startArrow`, `endArrow`, `startFill`, `endFill`. Zakończenia mają nazwy z draw.io: `classic`, `block`, `open`, `async`, `openAsync`, `doubleBlock`, `diamond`, `oval`, `box`, `circlePlus`, `halfCircle`, `baseDash`, `dash`, `cross`, `none` oraz zakończenia w notacji kurzej stopki: `ERone`, `ERmandOne`, `ERzeroToOne`, `ERmany`, `ERoneToMany`, `ERzeroToMany`. Ustawienie `startFill=0`/`endFill=0` pozostawia tylko kontur zakończenia. Element ze stylem jest rysowany jak w draw.io (domyślnie białe wypełnienie i czarny obrys), a kontener ze stylem przybiera własny kształt zamiast przerywanej ramki. Nieznane klucze są zachowywane.
- `prop "key" "value"` zapisuje dowolną właściwość (może występować wielokrotnie).
- `shape` przyjmuje jeden z podstawowych kształtów: `rect rounded ellipse circle rhombus triangle hexagon octagon cylinder
parallelogram trapezoid document cloud actor person note card step arrow process stadium cube
swimlane folder queue interface text image invisible frame module lifeline`.
- `layout fixed` zachowuje dokładnie każde ustawienie `pin … at X, Y size W, H` (nieprzypięte węzły ustawiają się w szeregu poniżej). `route "relationship id" via X, Y … from EX, EY to NX, NY` ustala punkty załamania krawędzi oraz miejsca, w których wychodzi ona ze źródła i wchodzi do celu (względem prostokąta elementu, w zakresie 0–1). Bez `from`/`to` krawędź wychodzi w kierunku pierwszego załamania. Przesunięcie węzła lub zmiana jego rozmiaru na płótnie widoku o stałym układzie aktualizuje jego `pin … at … size`, a przeciągnięcie punktów załamania aktualizuje `route` (zob. [Rysowanie ręczne w widokach o stałym układzie](guide/fixed-views.md)).

## Style i motywy

`theme` wybiera kolory używane do rysowania dokumentu. Bloki `style` nadają wspólny wygląd wszystkim elementom danego rodzaju, wszystkim obiektom z danym znacznikiem lub wszystkim relacjom:

```
theme dark

style container {
  fill "#438dd5"
  stroke "#3c7fc0"
}
style tag "external" { fill "#999999" dashed true }
style relationship tag "async" { dashed true endArrow open }

model { … }

view checkout of shop {
  include *
  theme default        // this view only
}
```

- Motywy: `default`, `dark` (ciemne tło) i `mono` (czerń na białym tle, szare wypełnienia, do druku). Ustawienie `theme` widoku ma pierwszeństwo przed ustawieniem dokumentu. Nieznana nazwa powoduje ostrzeżenie i użycie `default`. Bez instrukcji `theme` każdy widok używa `default`, tak jak wcześniej.
- Selektory: `*` (każdy element), rodzaj (`container`), `tag "…"`, rodzaj i znacznik (`container tag "legacy"`), `relationship` oraz `relationship tag "…"`. W selektorze `relationship` i `tag` są słowami kluczowymi, więc nie ma selektora rodzaju o żadnej z tych nazw.
- Każdy wiersz bloku ma postać `key value`. Klucze są takie same jak w zapisie `style "…"` bezpośrednio przy obiekcie (wymieniono je wyżej; nieznane klucze są zachowywane). Wartością może być ciąg znaków, liczba lub słowo. Wartości `true` i `false` są przechowywane jako `1` i `0`, tak jak zapisuje je draw.io.
- Kolejność od najniższego priorytetu: motyw, bloki `*`, bloki rodzaju, bloki znacznika, bloki rodzaju ze znacznikiem (w obrębie jednego poziomu wygrywa późniejszy blok), na końcu `style "…"` zapisany bezpośrednio przy obiekcie.
- Blok `style` zmienia tylko to, co jawnie ustawia: element zachowuje pozostałe kolory motywu i pogrubioną etykietę. Natomiast `style "…"` zapisany przy obiekcie nadal nadaje mu wygląd z draw.io (białe wypełnienie, czarny obrys) dla pominiętych ustawień. W przypadku rodzaju z elementami podrzędnymi bloki zmieniają styl ramki (`fill`, `stroke`, `strokeWidth`, `dashed`, `fontColor`).
- `style` i `theme` są instrukcjami najwyższego poziomu; `theme` może też wystąpić w widoku. Wewnątrz `model { … }` pozostają zwykłymi nazwami, a `style "…"` przy elemencie określa jego własny styl.
- `print` zapisuje najpierw deklaracje motywów, następnie wybrany motyw, a potem bloki w kolejności wystąpienia, po jednej parze `key value` na wiersz.

### Własne motywy

W dokumencie można zadeklarować motyw, podając jego treść w bloku, a następnie wybrać go tak samo jak motyw wbudowany:

```
theme brand extends dark {
  background "#101418"
  edge "#9aa5b1"
  fontFamily "IBM Plex Sans, sans-serif"
  node person { fill "#08427b" stroke "#052e56" text "#ffffff" }
  node external { fill "#444444" }
}

theme brand
```

- `extends` wskazuje motyw wbudowany, inny zadeklarowany motyw lub motyw z biblioteki. Bez `extends` punktem wyjścia jest `default`. Każdy motyw zmienia tylko to, co jawnie ustawia.
- Klucze motywu: `background`, `fontFamily`, `fontSize`, `smallFontSize`, `edge` i `edgeText` (linie i etykiety relacji), `boundary` i `boundaryText` (ramki kontenerów i grup), `surface` (paski aktywacji, niewypełnione groty), `dividerFill`, `labelText` (etykiety pod ikonami).
- `node <kind> { fill … stroke … text … }` określa kolory rodzaju; `node default` dotyczy każdego rodzaju bez własnego wpisu, a `node external` — elementów ze znacznikiem `external`.
- Motywy mogą też pochodzić z bibliotek (zob. [biblioteki](libraries.md#manifest)): po deklaracji `use "acme-brand@^1" as acme` instrukcja `theme acme.corporate` wybiera taki motyw, a `theme mine extends acme.corporate { … }` tworzy na jego podstawie własny.
- Zgłaszane są nieznane klucze, nieznane motywy wskazane przez `extends`, powtórzone nazwy, użycie nazw wbudowanych i cykle `extends`. Jeśli nie da się ustalić definicji motywu, do rysowania używany jest `default`.

## Widoki sekwencji

W widoku z `layout sequence` uwzględnione elementy są rysowane jako linie życia w kolejności podanej w `include`, a kroki z bloku `sequence` — od góry do dołu:

```
view login {
  include user web api
  layout sequence
  sequence {
    autonumber
    user -> web "Log in"          // ->  call, ->> async, --> reply
    activate web
    alt "valid" {
      web --> user "Welcome"
      note over web api "token issued"
    } else "invalid" {
      web --> user "Error"
      note right of web "logged"
    }
    deactivate web
    divider "Later"
    delay "5 minutes"
    ref over user web "Logout"
    loop "every 30 s" { web ->> api "heartbeat" }
  }
}
```

Dostępne ramki to `alt`, `opt`, `loop`, `par`, `critical`, `break` i `group`; każda może zawierać opcjonalne sekcje `else "label" { … }`. Komunikaty należą do widoku, nie do modelu: ci sami uczestnicy mogą się pojawiać w wielu widokach sekwencji, a relacje w modelu nadal przedstawiają tylko podsumowanie.

## Notatki i dekoracje

Notatki, swobodny tekst i ramki należą do jednego widoku, a nie do modelu. Pozwalają opisać diagram bez dodawania do niego elementów, więc pozostałe widoki tego samego modelu pozostają bez zmian.

```
view checkout of shop {
  include *
  layout layered direction right
  text "Checkout, container level" style "fontSize=18;fontStyle=1"
  note consistency "Orders are eventually consistent" below db
  note "Owned by team Web" left of web
  box pci "PCI scope" around api db
  note "Check the cache TTL"
  note "Draft" at 40, -80 size 160, 60
}
```

| Składnia | Efekt |
| --- | --- |
| `note [name] "text" [anchor] [style "…"]` | karteczka z notatką; po przypisaniu do elementu łączy się z nim przerywaną linią |
| `text [name] "text" [anchor] [style "…"]` | dowolny tekst, tytuł lub podpis |
| `box [name] ["label"] around a b … [style "…"]` | przerywana ramka z zaokrąglonymi narożnikami wokół prostokątów elementów, z etykietą w lewym górnym rogu |
| `box [name] ["label"] at X, Y size W, H [style "…"]` | ramka o stałych współrzędnych |

Zakotwiczenie określa położenie względem elementu widoku (`below`, `above`, `left of` lub `right of`) albo podaje współrzędne `at X, Y` z opcjonalnym rozmiarem `size W, H`. Bez zakotwiczenia teksty układają się jeden pod drugim nad diagramem, a notatki — w kolumnie po jego prawej stronie. Dekoracje są rozmieszczane po wyznaczeniu układu, więc nigdy nie przesuwają elementów. Jeśli dekoracja zasłaniałaby węzeł, etykietę krawędzi lub inną notatkę, odsuwa się dalej od swojego elementu. Tekst notatki zawija się przy szerokości 200 px.

- Nazwa jest opcjonalna i musi być unikatowa w widoku. Pozwala wskazać obiekt podczas edycji i w aplikacjach korzystających z silnika (w SVG jest zapisana jako `data-name`, a każda dekoracja ma `data-decoration="<index>"` i klasę `dd-decoration dd-note|dd-text|dd-box`).
- `style` przyjmuje klucze elementów: `fill`, `stroke`, `strokeWidth`, `dashed` (ramki są przerywane, chyba że ustawiono `dashed=0`), `rounded`, `opacity`, `fontColor`, `fontSize`, `fontStyle`, `align`, `verticalAlign`.
- Usunięcie elementu usuwa dekoracje, które są do niego przypisane lub otaczają go ramką.
- `note`, `text`, `box` i `around` są kontekstowymi słowami kluczowymi: elementy nadal mogą mieć takie nazwy. Wewnątrz `sequence { … }` zapisy `note over …` / `note left of …` zachowują znaczenie właściwe dla sekwencji.

## Reguły leksykalne

- Białe znaki nie mają znaczenia; instrukcje nie wymagają separatorów.
- Komentarze: `// to end of line` oraz `/* block */`. Oba rodzaje pozostają w drzewie składniowym.
- Identyfikatory: `[A-Za-z_][A-Za-z0-9_]*`. Odwołania do zagnieżdżonych elementów zapisuje się z kropkami: `shop.web`.
- Ciągi znaków: w podwójnym cudzysłowie, z sekwencjami specjalnymi zaczynającymi się od `\` (`\n` to podział wiersza), w jednym wierszu. **Ciąg blokowy** obejmuje wiele wierszy: zaczyna się od `"""` i kończy na następnym `"""`, z tymi samymi sekwencjami specjalnymi. Reszta wiersza po otwierającym `"""` i wiersz zamykającego `"""` są pomijane, jeśli są puste, podobnie jak wcięcie wspólne dla pozostałych wierszy, więc tekst można wciąć razem z otaczającą instrukcją. Puste wiersze w środku zostają:

  ```
  db = container "Orders DB" description """
    Orders and their lines.

    Archived after two years.
  """
  ```

  Ciągi blokowe działają wszędzie tam, gdzie zwykłe ciągi. `format` zapisuje wielowierszowy `description` jako ciąg blokowy, a każdy inny ciąg z `\n`.
- Liczby: `-?digits(.digits)?`.
- Słowa kluczowe są kontekstowe: `use as model view of group port north south east west tech shape tag
description style prop include exclude layout direction spacing right left down up pin below
above at size route via from to align horizontally vertically order sequence activate
deactivate note over divider delay ref autonumber alt opt loop par critical break else theme relationship extends node
text box around` są słowami kluczowymi tam, gdzie gramatyka oczekuje słowa kluczowego, a wszędzie indziej — zwykłymi nazwami (`layout = container "Layout"` jest poprawne). Gdy obie interpretacje są możliwe, na przykład po liście selektorów, po której następuje `layout …`, pierwszeństwo ma interpretacja jako słowo kluczowe.

## Gramatyka (zapis nieformalny)

```
Document     := (Use | ThemeDef | Theme | StyleBlock | ModelBlock | ViewBlock)*

Use          := "use" String ("as" Name)?          // see docs/libraries.md

Theme        := "theme" QName
ThemeDef     := "theme" Name ("extends" QName)? "{" (Name Value | "node" Name "{" (Name Value)* "}")* "}"
StyleBlock   := "style" StyleSelector "{" (Name (String | Number | Ident))* "}"
StyleSelector:= ("*" | "relationship" | Kind) ("tag" String)? | "tag" String

ModelBlock   := "model" ModelBody
ModelBody    := "{" ModelItem* "}"
ModelItem    := ElementDecl | Relationship | Group | Port

ElementDecl  := Name "=" Kind Label? Property* ModelBody?
Kind         := Ident | Alias "." Ident               // built in, or a library's kind
Relationship := (Name "=")? QName Arrow QName Label? Property*
Arrow        := "->" (solid) | "-->" (dashed) | "<->" (bidirectional)
Group        := "group" Label ModelBody
Port         := "port" Name ("north" | "south" | "east" | "west")?
Property     := "tech" String
              | "shape" QName                      // a base shape, or alias.shape from a library
              | "tag" String
              | "description" String
              | "style" String                     // "key=value;key=value"
              | "prop" String String               // repeatable

ViewBlock    := "view" Name ("of" QName)? "{" ViewItem* "}"
ViewItem     := "include" Selector+
              | "exclude" Selector+
              | "layout" Algorithm ("direction" Dir | "spacing" Number)*
              | "pin" QName (Placement QName | "at" Coord ("size" Coord)?)
              | "route" String ("via" Coord+)? ("from" Coord)? ("to" Coord)?
              | "size" QName Coord                 // width, height in this view
              | "rotate" QName Number              // degrees, clockwise
              | "align" QName QName+ ("horizontally" | "vertically")?
              | "order" QName QName+
              | "sequence" SeqBody
              | "theme" QName
              | ("note" | "text") Name? String Anchor? ("style" String)?
              | "box" Name? String? ("around" QName+ | "at" Coord "size" Coord)
                ("style" String)?
Selector     := "*" | QName
Algorithm    := Name                               // layered (default), tree, grid, radial, force, fixed, sequence
Dir          := "right" | "left" | "down" | "up"
Placement    := "below" | "above" | "left" "of" | "right" "of"
Coord        := Number "," Number
Anchor       := Placement QName | "at" Coord ("size" Coord)?

SeqBody      := "{" SeqItem* "}"
SeqItem      := QName MessageArrow QName Label?
              | ("activate" | "deactivate") QName
              | "note" ("over" QName+ | ("left" | "right") "of" QName) Label
              | "divider" Label?
              | "delay" Label?
              | "ref" "over" QName+ Label
              | "autonumber"
              | FrameKind Label? SeqBody ("else" Label? SeqBody)*
MessageArrow := "->" (call) | "->>" (async) | "-->" (reply)
FrameKind    := "alt" | "opt" | "loop" | "par" | "critical" | "break" | "group"

Label        := String
Kind         := Name
QName        := Name ("." Name)*
```

## Semantyka

| Składnia | Model |
| --- | --- |
| `web = container "Web app"` wewnątrz `shop` | element `shop.web`, rodzaj `container`, nazwa `Web app`, rodzic `shop` |
| brak etykiety | domyślną nazwą jest identyfikator |
| `tech`, `shape`, `tag`, `description` | pola elementu lub relacji o tych samych nazwach |
| `style "k=v;…"`, `prop "k" "v"` | styl i właściwości elementu lub relacji |
| `a -> b "label"` | relacja o identyfikatorze `a->b` (`a->b#2`, `#3`, … dla kolejnych wystąpień) |
| `pay = a -> b` | relacja z jawnie nadanym identyfikatorem `pay` |
| `a.http -> b` | jeśli `a` ma `port http`, punktem końcowym jest ten port |
| `group "Build" { … }` | grupa; zawarte w niej elementy mają ustawione pole `group` |
| `view v of shop` | widok, którego zakres obejmuje elementy podrzędne `shop` |
| `include *` | każdy element w zakresie; widok bez `include` domyślnie uwzględnia `*` |
| `layout …` | algorytm, kierunek i odstępy dla widoku (domyślny algorytm: `layered`) |
| `pin a below b` | `PinHint` z położeniem `Below` |
| `pin a at 10, 20 size 120, 60` | `PinAtHint` z pozycją i opcjonalnym rozmiarem (jawny wybór swobodnego rozmieszczenia) |
| `route "a->b" via 1, 2 from … to …` | `RouteHint`: punkty załamania, wyjścia i wejścia (współrzędne 0–1 względem prostokąta) |
| `size a 200, 80` | `SizeHint`: rozmiar elementu w tym widoku, zachowywany w każdym układzie |
| `rotate a 30` | `RotateHint`: obrót o 30° zgodnie z ruchem wskazówek zegara wokół środka elementu (tylko elementy bez dzieci) |
| `align a b horizontally` | `AlignHint` (domyślnie oś pozioma) |
| `order a b c` | `OrderHint` |
| `sequence { … }` | kroki sekwencji widoku; `autonumber` w dowolnym miejscu włącza numerowanie |
| `theme dark` | `model.theme` (w widoku: `view.theme`) |
| `theme brand extends dark { … }` | `model.themes[]`: `{ name, extends?, …keys, nodes? }` |
| `style container { fill "#438dd5" }` | `model.styles[]`: `{ target: 'elements', kind, tag?, style }` |
| `note n "x" right of a` | dekoracja `{ kind: 'note', name: 'n', placement: 'right-of' }` |
| `box "x" around a b` | dekoracja `{ kind: 'box', around: [a, b] }` |

Rodzaj elementu może być dowolnym identyfikatorem. Wbudowane rodzaje to: `person`, `system`, `container`, `component`, `database`, `process`, `decision`, `start`, `end`, `note`, `class`, `object`, `event`, `task`, `subprocess`, `gateway`, `data`, `annotation`, `group`, `pool`, `lane` i `conversation`. Inne rodzaje są akceptowane z ostrzeżeniem; kolejne rodzaje mogą pochodzić z bibliotek (zob. niżej). Element `person` jest rysowany jako ludzik z kresek, z nazwą pod spodem. Ponieważ nazwa leży bezpośrednio na płótnie, jej kolor pochodzi z `labelText` motywu (ustawienie `fontColor` stylu ma pierwszeństwo, a `style` zapisany przy elemencie zachowuje czerń z draw.io). Wypełnienie elementu określa kolor głowy, a ustawienie motywu `node person { text }` określa kolor nazwy tylko wtedy, gdy `shape` przedstawia osobę jako wypełniony prostokąt.

Dostępne algorytmy układu to `layered` (domyślny), `tree`, `grid`, `radial`, `force`, `fixed` i `sequence` (opis każdego z nich znajdziesz w [podręczniku użytkownika](guide/layouts.md)). Każda inna nazwa zostaje zachowana, ale uruchamiany jest silnik układu warstwowego, a w wierszu `layout` pojawia się ostrzeżenie.

## Rodzaje elementów z bibliotek

Biblioteka może dodawać rodzaje elementów (pole `kinds` w manifeście, zob. [libraries.md](libraries.md#rodzaje-elementów)). Dokument odwołuje się do nich przez alias biblioteki, tak samo jak do jej kształtów:

```
use "acme-platform@^1" as acme

model {
  orders = acme.service "Orders" tech "Go"
  events = acme.topic "Order events"
  jobs = acme.queue "Jobs" {
    worker = component "Worker"
  }
  orders -> events "publishes"
}
```

- Element rodzaju z biblioteki przybiera kształt przypisany do tego rodzaju (rysunek z biblioteki lub kształt podstawowy) i jego kolory, chyba że określa własny `shape` lub `style`. Jeśli ma elementy podrzędne, jest rysowany jako ramka z etykietą zawierającą stereotyp rodzaju (`Jobs [queue]`), tak jak rodzaje C4.
- Rodzaj jest przechowywany w zapisanej postaci, `acme.topic`, więc zachowuje się zarówno w modelu, jak i w tekście generowanym na jego podstawie.
- Jeśli aliasu rodzaju nie zadeklarowano w żadnym `use`, parser zgłasza ostrzeżenie. Jeśli biblioteka nie ma takiego rodzaju, `diadraw check`, `diadraw render` oraz edytor zgłaszają ostrzeżenie, tak jak w przypadku nieznanego kształtu, a element jest rysowany jako prostokąt w domyślnych kolorach. W obu przypadkach dokument nadal jest przekształcany w model.
- Rodzaje z bibliotek mają zawsze nazwę kwalifikowaną, więc nigdy nie kolidują z rodzajami wbudowanymi ani ze sobą nawzajem.

## Odwołania

Identyfikator relacji pochodzi od jej punktów końcowych, chyba że nadano jej nazwę. Identyfikatory wyprowadzone z końców relacji zależą od krawędzi: zmiana punktu końcowego zmienia identyfikator, a usunięcie pierwszej z dwóch równoległych krawędzi zmienia `a->b#2` w `a->b`. Przy ręcznej edycji tekstu przepada wtedy trasa `route` odwołująca się do starego identyfikatora (przepięcie na płótnie aktualizuje identyfikatory w takich trasach). Nadanie krawędzi nazwy, `pay = a -> b`, przypisuje jej identyfikator `pay` na stałe: zmiana punktów końcowych ani kolejności go nie zmienia, więc `route "pay" …` nadal obowiązuje (o tym, kiedy nazywać krawędzie, przeczytasz w sekcji [Identyfikatory relacji](guide/relationships.md#identyfikatory-relacji) w podręczniku użytkownika). Nazwa jest używana bez zmian niezależnie od miejsca deklaracji relacji (nie jest poprzedzana ścieżką elementu nadrzędnego). Musi być unikatowa wśród relacji, ale może się pokrywać z nazwą elementu. Nadanie nazwy jednej krawędzi nie przesuwa wyprowadzonych identyfikatorów pozostałych. Funkcja `print` zapisuje identyfikator będący nazwą jako `name = …`; identyfikatorów, które nie są nazwami (np. pochodzących z JSON), nie da się zapisać i po ponownym odczycie zostaną wyprowadzone z punktów końcowych.

Odwołanie z kropkami jest rozwiązywane od najbardziej wewnętrznego zakresu ku zewnętrznym: wewnątrz `shop { … }` zapis `web` oznacza `shop.web`, a `shop.web` działa wszędzie. Selektory i wskazówki w widoku są rozwiązywane w taki sam sposób względem zakresu widoku, więc `view checkout of shop { pin db below web }` jest poprawne. Jeśli ostatni człon odwołania jest portem poprzedzającego go elementu (`api.http`), odwołanie wskazuje na ten port.

## Diagnostyka

Parsowanie nigdy nie zgłasza wyjątków. Parser sam wznawia pracę po błędzie składni: oznacza instrukcję, której nie udało się rozpoznać, i pomija tekst do końca jej bloku lub do następnego wiersza rozpoczynającego poprawną w tym miejscu instrukcję. Dzięki temu pozostała część pliku nadal jest przekształcana w model. Pominięty tekst jest zgłaszany jako `Unexpected "…"`, a urwana instrukcja jako `Syntax error: incomplete statement`.

Błędy modelu pochodzą z operacji rdzenia i z rozwiązywania odwołań. Wskazują fragment tekstu, którego dotyczy problem: nieznane elementy, nieznane relacje w `route`, powtórzone identyfikatory, porty poza elementem lub zadeklarowane dwukrotnie, niepoprawne nazwy w `use` lub powtórzone aliasy. Ostrzeżenia dotyczą nieznanych rodzajów elementów, rodzajów bibliotecznych bez aliasu zadeklarowanego w `use` oraz użycia `shape` lub `description` w relacji (są wtedy ignorowane). Wiersze i kolumny są numerowane od 1. Kolumny liczy się w jednostkach kodowych UTF-16, tak jak w edytorach. Polecenie `diadraw check file.draw` wypisuje komunikaty w postaci `file:line:column: error|warning: message`.

## Zasady edycji i ponownego zapisu

1. **Bezstratne parsowanie.** Drzewo zachowuje komentarze i białe znaki.
2. **Zmiany semantyczne stają się poprawkami w tekście.** Dodanie obiektu, zmiana nazwy lub przepięcie krawędzi na płótnie zmienia najmniejszy możliwy fragment tekstu we właściwym miejscu. Przepięcie aktualizuje tylko przeniesiony punkt końcowy, zapisując go jako najkrótsze odwołanie wskazujące nowy element z miejsca deklaracji relacji. Aktualizuje też każdą trasę `route`, której identyfikator relacji ulega zmianie (nazwana relacja zachowuje identyfikator; identyfikatory wyprowadzone są ustalane od nowa, więc numeracja równoległych krawędzi może się zmienić; zob. [podręcznik użytkownika](guide/relationships.md#przepinanie-linii)). Edycja właściwości elementu w inspektorze (rodzaju, nazwy, `tech`, `shape`, `tag`, `description`, `style`, `prop`) aktualizuje tylko zmienione klauzule: zmieniona wartość jest zastępowana na miejscu, wyczyszczona — usuwana, a nowa — dopisywana po ostatniej klauzuli deklaracji. Komentarze, kolejność klauzul i treść bloku elementu pozostają bez zmian (zob. [podręcznik użytkownika](guide/element-properties.md)). W taki sam sposób edytowane są etykieta relacji, `tech`, strzałka, `tag`, `style` i `prop`. Nowa nazwa jest dopisywana na początku wiersza jako `name = ` (lub usuwana wraz z tym zapisem). Nowy identyfikator jest podstawiany w każdej trasie `route` odwołującej się do zmienionego identyfikatora, także wyprowadzonego, którego numer zmienił się wskutek tej operacji. Edycja bloków stylów i motywów z płótna działa podobnie, wpis po wpisie: zmieniona wartość jest zastępowana na miejscu, wpis usuniętego klucza znika, nowy klucz jest dopisywany na końcu bloku, a nowe bloki i instrukcje motywów trafiają obok innych wpisów tego samego rodzaju (zob. [podręcznik](guide/styles-and-themes.md)).
3. **Zmiany geometrii stają się wskazówkami, nie współrzędnymi.** Przeciągnięcie zapisuje `pin`, `align` lub `order`.
4. **Swobodne obiekty również mają zapis tekstowy.** Notatki, tekst i ramki należą do widoku (`note`, `text`, `box`; zob. [Notatki i dekoracje](#notatki-i-dekoracje)).
5. **Import zamiast zgodności składniowej.** Diagramy PlantUML, Mermaid, Structurizr i draw.io są importowane do modelu; ich składnia nie jest natywnie analizowana przez parser.

## Otwarte kwestie

Otwarte zadania dotyczące języka DSL i jego edycji z płótna są śledzone na [liście zadań](backlog.md#dsl).
