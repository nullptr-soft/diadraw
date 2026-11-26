# Style i motywy na płótnie

O wyglądzie diagramu decydują trzy rzeczy. **Motyw** określa kolory i czcionki używane do rysowania całego diagramu. **Bloki stylów** nadają wspólny wygląd wszystkim elementom danego rodzaju, wszystkim obiektom z danym tagiem lub wszystkim relacjom. **Styl wpisany bezpośrednio w deklaracji elementu** (pole Styl w [inspektorze](element-properties.md)) zmienia tylko ten jeden element. W tekście wygląda to tak:

```
theme brand extends dark {
  background "#101418"
  node person { fill "#08427b" stroke "#052e56" text "#ffffff" }
}

theme brand

style container {
  fill "#438dd5"
}
style tag "external" { fill "#999999" dashed true }
style relationship tag "async" { dashed true }
```

Wszystkie selektory i klucze znajdziesz w [dokumentacji DSL](../dsl.md#style-i-motywy). Ten rozdział opisuje pracę na płótnie: możesz edytować wszystkie te ustawienia bez opuszczania diagramu, a każda zmiana i tak trafi do tekstu.

## Kiedy użyć którego ustawienia

- Użyj **bloku stylu**, gdy wygląd zależy od rodzaju lub tagu: „każda baza danych jest pomarańczowa”, „wszystko z tagiem `external` jest szare i ma kreskowany obrys”. Nowe elementy tego rodzaju lub z tym tagiem automatycznie otrzymają taki sam wygląd.
- Użyj pola **Styl** danego elementu, gdy tylko on ma wyglądać inaczej.
- Użyj **motywu**, aby ustawić wygląd całego diagramu: tło, kolor linii, czcionkę i domyślne kolory poszczególnych rodzajów. Wybierz motyw wbudowany (`default`, `dark`, a do druku `mono`) lub utwórz własny.

Gdy kilka ustawień dotyczy tego samego obiektu, pierwszeństwo ma bardziej szczegółowe. Kolejność od najmniej do najbardziej szczegółowego jest następująca: motyw, bloki `*`, bloki rodzaju, bloki tagu, bloki rodzaju z tagiem i wreszcie własny styl elementu.

## Style wspólne elementu

Zaznacz jeden element, a na dole inspektora pojawi się pole **Style wspólne**. Zawiera ono bloki stylów dotyczące tego elementu, po jednym w wierszu, w kolejności zapisu w dokumencie:

```
container: fill=#438dd5
tag "external": fill=#999999;dashed=1
```

W każdym wierszu podaj selektor, dwukropek i klucze w takim zapisie jak w polu Styl (pary `key=value` oddzielone `;`). Zmiana w tym polu zmienia odpowiednie bloki:

| Co robisz | Co zmienia się w tekście |
| --- | --- |
| Zmieniasz wartość | Zmienia się odpowiedni wiersz w bloku. |
| Dodajesz klucz do wiersza | Klucz trafia na koniec bloku. |
| Usuwasz klucz | Wiersz tego klucza znika z bloku. |
| Dodajesz wiersz, np. `database: fill=red` | Za wyświetlonymi blokami powstaje nowy blok `style`. |
| Usuwasz wiersz | Znika odpowiadający mu blok. |
| Zmieniasz selektor w wierszu | Zmienia się selektor bloku, ale jego treść pozostaje bez zmian. |

Zmiana w tym polu celowo wpływa na wygląd **każdego** elementu, którego dotyczy blok, a nie tylko zaznaczonego. Aby zmienić wygląd tylko tego elementu, użyj jego pola Styl.

Pole przyjmuje tylko selektory pasujące do zaznaczonego elementu: jego rodzaj, jeden z jego tagów, jego rodzaj z jednym z jego tagów lub `*`. Aplikacja odrzuci wiersz dotyczący innego rodzaju lub relacji i wyświetli komunikat na pasku stanu. Aby edytować takie bloki, usuń zaznaczenie (zobacz poniżej).

## Motyw i style dokumentu

Kliknij puste miejsce na płótnie. Gdy nic nie jest zaznaczone, panel właściwości obok płótna pokazuje ustawienia dokumentu:

- **Motyw**: instrukcja `theme` dokumentu. Wpisz `default`, `dark`, `mono`, nazwę zadeklarowanego przez siebie motywu lub nazwę motywu z biblioteki (`acme.corporate`). Wyczyszczenie pola usuwa instrukcję.
- **Motyw tego widoku**: instrukcja `theme` wewnątrz bieżącego widoku. Zastępuje motyw dokumentu tylko w tym widoku. Puste pole oznacza, że widok używa motywu dokumentu.
- **Bloki stylów**: wszystkie bloki stylów w dokumencie, także te dotyczące relacji, np. `relationship tag "async": dashed=1`. Podobnie jak wyżej, każdy blok zajmuje jeden wiersz.

### Tworzenie własnego motywu

W polu Motyw (lub Motyw tego widoku) wpisz nazwę, której nie ma jeszcze żaden motyw, np. `night`. DiaDraw zadeklaruje nowy motyw na podstawie tego, który zastępujesz, i wybierze go:

```
theme night extends dark {}

theme night
```

Gdy widok używa motywu zadeklarowanego w dokumencie, w panelu pojawia się jego sekcja, **Motyw night**, z trzema dodatkowymi polami:

- **Rozszerza**: motyw bazowy. Puste pole oznacza `default`.
- **Klucze motywu**: jedna para `key=value` w wierszu. Dostępne klucze to `background`, `fontFamily`, `fontSize`, `edge`, `edgeText`, `boundary` i pozostałe wymienione w [dokumentacji DSL](../dsl.md#własne-motywy).
- **Kolory węzłów**: jeden rodzaj w wierszu wraz z jego kluczami `fill`, `stroke` i `text`: `person: fill=#08427b;stroke=#052e56;text=#ffffff`. Wpis `default` obejmuje wszystkie rodzaje bez własnego wiersza, a `external` — wszystkie elementy z tagiem `external`. Klucz `text` określa kolor nazwy osoby tylko wtedy, gdy `shape` nadaje jej postać wypełnionego pola. Nazwa pod postacią złożoną z kresek jest rysowana na płótnie w kolorze `labelText`.

Motywów wbudowanych i bibliotecznych nie można edytować bezpośrednio. Aby je dostosować, utwórz własny motyw, który je rozszerza.

### W aplikacjach na macOS i Linux oraz w aplikacji internetowej

W aplikacjach pola te mają postać kontrolek, które zapisują ten sam tekst:

- **Motyw**, **Motyw tego widoku** i **Rozszerza** to pola z listą motywów wbudowanych, motywów z używanych bibliotek i motywów zadeklarowanych w dokumencie. Nadal możesz wpisać nową nazwę.
- **Bloki stylów** to lista z jednym wierszem na blok. Wybierz lub wpisz selektor (`*`, `relationship`, rodzaj, `tag "external"`, `relationship tag "async"`). Kliknij trójkąt przy wierszu, aby rozwinąć ten sam edytor stylu co w inspektorze elementu i zmienić klucze. Przycisk − usuwa blok, a **Dodaj blok stylu** dodaje wiersz.
- **Klucze motywu** zawierają przycisk dla każdego klucza koloru (`background`, `edge`, `boundary`, …) oraz pola tekstowe dla `fontFamily` i rozmiarów czcionek. Przyciski kolorów otwierają tę samą paletę próbek co w inspektorze elementu. Opcja Niestandardowy… pozwala wybrać dowolny inny kolor. Przycisk × obok koloru usuwa klucz i przywraca kolor z motywu bazowego. Klucze niewymienione w panelu pozostają bez zmian.
- **Kolory węzłów** mają po jednym wierszu na rodzaj z przyciskami kolorów wypełnienia, linii i tekstu. Aby utworzyć wiersz, wybierz rodzaj i kliknij Dodaj. Wiersz trafi do tekstu, gdy przypiszesz mu kolor.

W aplikacji internetowej pola motywów podpowiadają motywy wbudowane i zadeklarowane w dokumencie. Trójkąt przy bloku stylu zwija blok do jednowierszowego zestawienia jego kluczy.

W aplikacji na Linux (GTK) Motyw, Motyw tego widoku i Rozszerza to pola z przyciskiem ▾ wyświetlającym listę motywów. Bloki stylów to lista, na której ▸ otwiera edytor stylu. W polach Klucze motywu i Kolory węzłów kolory wybiera się za pomocą przycisków.

## Zatwierdzanie zmian

Pola działają tak samo jak w inspektorze elementu: po zmianie naciśnij Enter lub opuść pole, aby zatwierdzić wartość. Escape przywraca poprzednią wartość. Pola wielowierszowe (Style wspólne, Bloki stylów, Klucze motywu, Kolory węzłów) zatwierdzasz przez ich opuszczenie lub, w aplikacji internetowej, za pomocą ⌘Enter / Ctrl+Enter. W aplikacji na macOS Return zatwierdza pole Style wspólne, a ⌥Return zaczyna nowy wiersz. W obu aplikacjach wybór koloru lub opcji w kontrolkach motywu i stylów działa od razu.

Każda zmiana to jeden krok we wspólnej historii cofania panelu i edytora tekstu: ⌘Z po dowolnej stronie cofa zmianę.

## Co się dzieje z tekstem

Tekst nadal jest źródłem prawdy, a panel zmienia w nim tylko to, co konieczne:

- zmienioną wartość zastępuje w miejscu jej zapisu, dzięki czemu komentarze i wyrównanie w tym wierszu pozostają bez zmian;
- usuwa wiersz usuwanego klucza; blok zapisany w jednym wierszu, np. `style tag "external" { fill "#999999" dashed true }`, nadal zajmuje jeden wiersz;
- nowe klucze trafiają na koniec bloku, nowe bloki — za pozostałe bloki stylów (lub nad blok `model`, jeśli to pierwszy blok stylu), a nowa instrukcja wyboru motywu — za deklaracje motywów;
- kolejność zapisu w dokumencie pozostaje bez zmian niezależnie od tego, w jakiej kolejności wpiszesz pozycje w polu.

Na przykład po zmianie koloru wypełnienia kontenera w panelu zapis:

```
style container {
  fill "#438dd5"   // brand blue
  stroke "#3c7fc0"
}
```

zmieni się na:

```
style container {
  fill "#1168bd"   // brand blue
  stroke "#3c7fc0"
}
```

## Warto wiedzieć

- **`true` i `false`** są przechowywane jako `1` i `0`, tak jak zapisuje je draw.io, więc `dashed true` pojawia się w polach jako `dashed=1`. Jeśli nie zmienisz wpisu, w tekście pozostanie użyte przez ciebie słowo.
- **Wartości** w polach stylów nie mogą zawierać `;`, tak samo jak w stylach wpisanych bezpośrednio w deklaracji. Każdy klucz motywu zajmuje osobny wiersz, więc jego wartość może zawierać średnik.
- **Kilka bloków z tym samym selektorem** zajmuje kilka wierszy. Jeśli ustawiają ten sam klucz, obowiązuje wartość z późniejszego bloku.
- **Motyw z dwoma blokami `node` dla tego samego rodzaju** zostanie zapisany na nowo, gdy zmienisz kolory tego rodzaju w panelu, ponieważ panel nie może ustalić, który blok chcesz zmienić.
- **Zmiana nazwy lub usunięcie deklaracji motywu** wymaga edycji tekstu. Panel służy do edycji motywu, którego używa widok.
- **Dostępność:** aplikacja internetowa oraz aplikacje na macOS i Linux (GTK). Aplikacja na Windows nie ma jeszcze inspektora.
