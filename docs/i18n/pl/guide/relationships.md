# Relacje

Relacja to linia łącząca dwa elementy. Może oznaczać wywołanie, przepływ danych lub zależność. Podobnie jak elementy, relacje deklarujesz raz w bloku `model`. Pojawią się w każdym widoku, w którym widać oba ich końce.

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app"
    db  = container "Orders DB"
    web -> db "writes" tech "SQL"
  }
  customer -> shop.web "places order"
}
```

## Strzałki

| Strzałka | Wygląd | Typowe zastosowanie |
| --- | --- | --- |
| `->` | linia ciągła z jednym grotem | wywołanie, żądanie, zależność |
| `-->` | linia kreskowana | przepływ asynchroniczny lub pośredni |
| `<->` | groty na obu końcach | wymiana dwukierunkowa |

Po elemencie docelowym możesz podać etykietę oraz dowolne spośród właściwości `tech`, `tag`, `style` i `prop`, tak jak dla elementów. Na linii na diagramie widać etykietę i technologię.

## Zakończenia i style linii

Strzałka określa domyślny wygląd linii. Aby uzyskać inny efekt, np. otwarty grot, romb, zakończenie w notacji kurzej stopki, linię kropkowaną, podwójną lub zakrzywioną, nadaj relacji styl. Zobacz [Zakończenia i style linii](line-styles.md).

## Punkty końcowe i porty

Punktem końcowym jest element. Zapisz odwołanie do niego tak, jak wymaga tego miejsce deklaracji relacji: `web` wewnątrz `shop { … }`, a `shop.web` w dowolnym miejscu. Jeśli element deklaruje port (`api = container { port http north }`), zapis `api.http` przyłącza linię do tego portu.

## Przepinanie linii

Aby skierować linię do innego elementu, kliknij ją. Na każdym końcu zaznaczonej linii pojawi się małe kółko. Przeciągnij jedno z nich na inny element i upuść. Podczas przeciągania element pod wskaźnikiem jest podświetlony. Jeśli upuścisz kółko w pustym miejscu lub na elemencie, którego linia już dotyka, nic się nie zmieni.

Tekst zmienia się tylko w miejscu, w którym zapisano ten koniec:

```
web -> legacy "REST" tech "HTTPS"  // being migrated
```

po przeciągnięciu grotu z Legacy API na API zmienia się w:

```
web -> api "REST" tech "HTTPS"  // being migrated
```

Etykieta, `tech`, tagi, komentarze i strzałka pozostają bez zmian. Nowy koniec zostaje zapisany w najkrótszej postaci, która wskazuje element z miejsca deklaracji linii: `api` wewnątrz `shop { … }`, a `shop.api` poza tym blokiem. W rzadkich przypadkach żadna nazwa nie pozwala wskazać elementu z tego miejsca (np. wewnątrz `shop` element `shop.db` przesłania `db` z najwyższego poziomu). Aplikacja formatuje wtedy plik na nowo i informuje o tym na pasku stanu.

Warto pamiętać o kilku zasadach:

- **Tylko elementy, nie porty.** Upuszczenie końca na element przyłącza go do samego elementu. Aby przyłączyć linię do portu (`api.http`), edytuj tekst. Jeśli koniec jest już przyłączony do portu, upuszczenie go z powrotem na ten sam element zachowuje port.
- **Możesz też wskazać kontener.** Upuszczenie końca na obramowanie kontenera, z dala od elementów w środku, przyłącza linię do kontenera.
- **Bez pętli własnych.** Nie możesz upuścić jednego końca na element, do którego przyłączony jest drugi.
- **Nie można przepiąć linii narysowanych przez sam widok.** Gdy widok ukrywa element, jego linie prowadzą do najbliższego widocznego przodka (jest to relacja domniemana). W takiej postaci nie są zapisane w tekście, więc nie mają uchwytów. Przepnij rzeczywistą linię w widoku pokazującym oba końce lub zmień tekst.
- **Cofnij** (⌘Z lub Ctrl+Z) przywraca poprzedni koniec zarówno w tekście, jak i na płótnie, podobnie jak przy innych zmianach dokonanych na płótnie.

To, co stanie się z identyfikatorem linii i odwołaniami do niego, zależy od tego, czy linia ma nazwę. W następnej sekcji opisano identyfikatory, a dalej — [wpływ przepięcia na identyfikatory](#wpływ-przepięcia-na-identyfikatory).

## Identyfikatory relacji

Każda relacja ma identyfikator. Nie widać go na diagramie, ale inne części DiaDraw używają go, aby wskazać konkretną linię:

- **Ręczne wyznaczanie trasy.** Gdy [przeciągasz punkty załamania linii](fixed-views.md#zaginanie-linii) na płótnie lub importujesz diagram z draw.io, kształt linii zostaje zapisany w widoku jako `route "<id>" via …`.
- **Edycja na płótnie.** Identyfikator wskazuje linię podczas zaznaczania, zmiany etykiety i usuwania. Służy też do wprowadzania drobnych zmian, które edytor płótna zapisuje w pliku tekstowym.
- **Eksport do SVG.** Każda linia jest rysowana jako `<g class="dd-edge" data-id="<id>">`. Strona internetowa, test lub arkusz stylów może więc ją odnaleźć, np. w celu wyróżnienia `[data-id="checkout"]`.
- **Przegląd zmian.** Dzięki stałemu identyfikatorowi w porównaniu wersji widać, że linia prowadzi teraz gdzie indziej, a nie że jedną linię usunięto, a drugą dodano.

### Identyfikatory wyprowadzone z końców relacji

Jeśli nie nadasz relacji nazwy, jej identyfikator wynika z punktów końcowych: `web -> db` ma identyfikator `web->db`, a `web -> api.http` — `web->api.http`. Druga linia między tymi samymi elementami ma identyfikator `web->db#2`, następna dostaje przyrostek `#3` itd.

To wygodne, ale taki identyfikator zależy od końców linii. Zmieni się, gdy:

- **przepniesz linię**: `web -> legacy` zmienia się w `web -> api`, więc identyfikator zmienia się z `web->legacy` na `web->api`;
- **usuniesz wcześniejszą linię równoległą**: jeśli po `order -> billing` występuje drugi wpis `order -> billing`, usunięcie pierwszego zmienia numerację i druga linia dostaje zamiast `order->billing#2` identyfikator `order->billing`.

Gdy zmieniasz końce linii w tekście, odwołania do starego identyfikatora przestają działać. Dla wpisu `route` wskazującego ten identyfikator pojawi się komunikat `Unknown relationship`, a ręcznie ustawione punkty załamania przepadną. Jeśli przepniesz linię na płótnie, aplikacja sama zaktualizuje odwołania (zobacz niżej).

### Nazwane relacje

Aby nadać relacji własny identyfikator, poprzedź jej zapis nazwą i znakiem `=`:

```
model {
  web    = container "Web"
  api    = container "API"
  legacy = container "Legacy API"

  calls = web -> legacy "REST" tech "HTTPS"
}

view main {
  route "calls" via 200, 80
}
```

Teraz `calls` jest stałym identyfikatorem. Nawet jeśli zmienisz linię na `calls = web -> api`, trasa nadal będzie obowiązywać. Możesz też nadać linii nazwę na płótnie: zaznacz ją i wpisz nazwę w polu Nazwa w inspektorze, a trasy zostaną zaktualizowane automatycznie (zobacz [Nadawanie nazwy relacji w inspektorze](element-properties.md#nadawanie-nazwy-relacji-w-inspektorze)). Linie z nazwą i bez niej mogą występować obok siebie:

```
order -> billing "charge"
refund = order -> billing "refund" tag "async"
order -> billing "audit"
```

Ich identyfikatory to `order->billing`, `refund` i `order->billing#2`. Nadanie nazwy jednej linii nie zmienia numeracji pozostałych.

Zasady nadawania nazw:

- Nazwa jest identyfikatorem (litery, cyfry i `_`; nie może zaczynać się cyfrą).
- Nazwa obowiązuje dokładnie w zapisanej postaci, nawet wewnątrz elementu: `writes = web -> db` wewnątrz `shop { … }` ma identyfikator `writes`, nie `shop.writes`.
- Nazwa musi być unikatowa wśród relacji; drugi wpis `x = …` jest błędem. Może natomiast być taka sama jak nazwa elementu, ponieważ elementy i relacje mają odrębne identyfikatory.
- Słowa takie jak `group`, `port` i `layout` mogą być nazwami: po strzałce DiaDraw rozpoznaje, że wiersz opisuje relację.

### Kiedy nadać relacji nazwę

Nadaj relacji nazwę, jeśli coś poza samą linią od niej zależy:

- trasę linii wyznaczono ręcznie lub zamierzasz to zrobić;
- między tymi samymi elementami jest kilka linii i być może usuniesz niektóre z nich lub zmienisz ich kolejność;
- spodziewasz się przepiąć linię w miarę zmian projektu, np. podczas migracji z jednej usługi do innej;
- strona, test lub arkusz stylów odwołuje się do linii w wyeksportowanym SVG.

Jeśli żaden z tych przypadków nie zachodzi, pozostaw linię bez nazwy: wystarczy identyfikator wyprowadzony z jej końców, a tekst będzie krótszy.

### Wpływ przepięcia na identyfikatory

Gdy przepinasz linię na płótnie:

- **Nazwana linia zachowuje nazwę.** Zapis `calls = web -> legacy` zmienia się w `calls = web -> api`, a `route "calls" …` nadal obowiązuje.
- **Linia bez nazwy otrzymuje identyfikator wynikający z nowych końców.** Identyfikator `web->legacy` zmienia się w `web->api`, a każdy wpis `route`, który wskazywał stary identyfikator, zostaje zaktualizowany. Punkty załamania pozostają więc przypisane do tej samej linii.
- **Zmienia się numeracja linii równoległych, a wraz z nią odwołania w trasach.** Jeśli masz dwie linie `order -> billing` bez nazwy i przepniesz pierwszą do `ledger`, identyfikator drugiej zmieni się z `order->billing#2` na `order->billing`. Wpis trasy pierwszej linii będzie wskazywał `order->ledger`, a drugiej — `order->billing`, więc żadna z tras nie przejdzie na drugą linię. Działa to również w drugą stronę: jeśli przepniesz linię do pary elementów, między którymi biegnie już linia zapisana dalej w tekście, ta późniejsza linia otrzyma przyrostek `#2`.
- **Linia pozostaje zaznaczona** pod nowym identyfikatorem.

Po przepięciu linii jej trasa zachowuje punkty załamania. Wyznaczono je względem poprzednich końców, więc linia może teraz biec okrężną drogą. Przeciągnij punkty załamania w nowe miejsca lub usuń wiersz `route` z widoku, aby algorytm układu wyznaczył trasę.

Tylko wpisy tras wskazują w tekście linię bez nazwy, więc tylko te odwołania zostają przepisane. Strona, test lub arkusz stylów wyszukujący linię w wyeksportowanym SVG za pomocą `data-id` znajduje się poza plikiem i nie zostanie zaktualizowany. Jeśli istnieje takie odwołanie, nadaj linii nazwę.

### Identyfikatory, których nie można zapisać jako nazw

Model można też wczytać z JSON lub utworzyć programowo. Identyfikatory jego relacji mogą nie być poprawnymi nazwami (np. `edge-1`). Przy zapisie takiego modelu w postaci tekstu te linie nie otrzymują nazwy, więc po ponownym wczytaniu tekstu ich identyfikatory zostaną wyprowadzone z końców relacji.

## Zobacz też

- [Dokumentacja DSL: semantyka](../dsl.md#semantyka) — dokładne zasady.
- [Diagramy swobodne](../dsl.md#diagramy-swobodne) — `route` i inne wskazówki dla algorytmu układu.
- [Rysowanie ręczne w widokach o stałym układzie](fixed-views.md) — zaginanie linii na płótnie.
