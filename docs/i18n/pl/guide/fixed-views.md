# Rysowanie ręczne w widokach o stałym układzie

W większości widoków wystarczy określić, co ma się w nich znaleźć. O rozmieszczenie elementów dba algorytm, a w razie potrzeby możesz je skorygować wskazówką, taką jak `pin db below web`. Czasem jednak chcesz samodzielnie ustalić dokładne położenie i rozmiar każdego elementu oraz wskazać, gdzie mają się załamywać linie. Do tego służy `layout fixed`. Z tego układu korzystają diagramy importowane z draw.io; możesz go też użyć w dowolnym własnym widoku:

```
view checkout {
  include *
  layout fixed
  pin web at 40, 40 size 160, 70
  pin db at 320, 200
  route "writes" via 380, 90
}
```

`pin … at X, Y` umieszcza lewy górny narożnik elementu we wskazanym punkcie, a `size W, H` określa jego szerokość i wysokość (jeśli pominiesz rozmiar, element zachowa rozmiar potrzebny do wyświetlenia etykiety). W wierszu `route "id" via X, Y …` podajesz [identyfikator relacji](relationships.md#identyfikatory-relacji) oraz kolejne punkty załamania jej linii. Elementy bez wiersza `pin` ustawiają się w rzędzie pod przypiętymi elementami.

Wszystko to możesz zapisać w tekście albo po prostu narysować na płótnie.

## Przesuwanie

Przeciągnij element, aby go przesunąć. W widoku o stałym układzie element trafia dokładnie tam, gdzie go upuścisz, niezależnie od tego, czy trzymasz klawisz Option. Wiersz `pin … at` otrzymuje nowe współrzędne. Dotychczasowy rozmiar pozostaje bez zmian.

## Zmiana rozmiaru

Zaznacz jeden element. W jego narożnikach pojawią się cztery małe kwadratowe uchwyty. Przeciągnij jeden z nich, aby zmienić rozmiar: przeciwległy narożnik pozostaje na miejscu, a podczas przeciągania nad elementem widać jego nowy rozmiar. Minimalny rozmiar to 10 × 10.

Po zwolnieniu przycisku myszy wiersz `pin` zawiera nowe położenie i rozmiar. Na przykład:

```
  pin web at 40, 40   // storefront
```

zmienia się w:

```
  pin web at 40, 40 size 180, 82   // storefront
```

## Zaginanie linii

Zaznacz jedną linię, aby zobaczyć jej uchwyty:

- W każdym **punkcie załamania** znajduje się okrągły uchwyt. Przeciągnij go, aby przesunąć załamanie.
- **Na środku każdego odcinka** znajduje się mniejszy, jaśniejszy uchwyt. Przeciągnij go, aby dodać tam załamanie.
- Aby **usunąć** załamanie, przeciągnij jego punkt na prostą łączącą dwa sąsiednie punkty.

Po zwolnieniu przycisku myszy wiersz `route` zawiera kolejno wszystkie punkty załamania linii:

```
  route "writes" via 380, 90 380, 150
```

Jeśli trasa ma już `from` lub `to` (punkty, w których linia wychodzi z elementu i wchodzi do drugiego), te ustawienia pozostają bez zmian. Jeśli nie pozostał żaden punkt załamania i nie ma ani `from`, ani `to`, wpis trasy znika, a linia znów jest prosta.

Trasę relacji bez nazwy wskazuje jej identyfikator wyprowadzony z końców relacji, np. `route "web->db"`. Jeśli zmienisz któryś koniec relacji lub usuniesz równoległą relację, identyfikator się zmieni i trasa przestanie obowiązywać. Dlatego gdy ręcznie nadajesz linii kształt, [nadaj relacji nazwę](relationships.md#kiedy-nadać-relacji-nazwę), np. `writes = web -> db`.

## Co się dzieje z tekstem

Tak jak każda edycja na płótnie, ta operacja wprowadza niewielką zmianę w pliku. To jeden krok we wspólnej historii płótna i edytora tekstu: ⌘Z w każdym z nich cofa zmianę.

- Istniejący wiersz `pin` lub `route` jest aktualizowany **w tym samym miejscu**. Komentarz na jego końcu pozostaje, a w widoku systemu nazwa zachowuje zapisaną przez Ciebie postać (`pin web`, a nie `pin shop.web`).
- Nowy wiersz `pin` lub `route` jest dopisywany po pozostałych wierszach widoku.
- Współrzędne są zaokrąglane do liczb całkowitych.

## Warto wiedzieć

- **Punkty załamania są dostępne tylko w widokach o stałym układzie.** Widoki z układem automatycznym nie pokazują uchwytów załamań, ponieważ przebieg linii wyznacza algorytm. Rozmiar i obrót elementu możesz zmienić w każdym widoku; zobacz [Zmiana rozmiaru i obracanie](resize-and-rotate.md). Aby rysować ręcznie, dodaj `layout fixed` do widoku. Możesz też pozostać przy układzie automatycznym i sterować nim za pomocą wskazówek.
- **Tylko jeden obiekt naraz.** Uchwyty pojawiają się, gdy zaznaczysz dokładnie jeden element lub jedną linię.
- **Linie ortogonalne** (`edgeStyle=orthogonalEdgeStyle`) zachowują kąty proste: po przesunięciu punktu załamania linia otrzymuje dodatkowe załamania tam, gdzie są potrzebne, aby nadal biegła pod kątem prostym.
- **Dostępność:** w aplikacji internetowej oraz w aplikacjach na macOS, Linux (GTK) i Windows.
