# Przesuwanie elementów

Przeciągnij element, aby go przesunąć. Pozostanie tam, gdzie go upuścisz, w każdym widoku, niezależnie od układu. Po zwolnieniu przycisku myszy w widoku zapisuje się jeden wiersz:

```
view main {
  include *
  layout layered direction down
  pin db at 262, 246
}
```

Podobnie jak `size` czy `rotate`, ten wiersz należy tylko do danego widoku. Ten sam element może więc zajmować różne miejsca w różnych widokach. W układzie automatycznym algorytm nadal rozmieszcza pozostałe elementy. Jeśli upuścisz element na inny, algorytm odsunie ten drugi i na nowo poprowadzi dochodzące do niego linie. Gdy ponownie przeciągniesz ten sam element, jego wiersz `pin … at` otrzyma nowe współrzędne.

## Ustawianie względem sąsiadów

Podczas przeciągania przytrzymaj Option (Alt na Windows i Linuksie), aby zamiast współrzędnych określić położenie elementu względem innych. Nad przeciąganym elementem widać wiersz, który zapisze się po jego upuszczeniu:

- Jeśli element jest wyrównany z innym w granicach tolerancji 14 pikseli, zapisze się `align api db horizontally` (lub `vertically`).
- W przeciwnym razie zapisze się `pin db right of api` (lub `left of`, `above`, `below`) względem najbliższego elementu.

Znaczenie tych wierszy nie zmienia się wraz z układem, dlatego przydają się w diagramach, które stale się rozrastają. Jeśli wskazówka jest już spełniona, np. dwa elementy już leżą w tym samym rzędzie, element pozostaje w miejscu wyznaczonym przez algorytm.

W widoku z `layout fixed` upuszczenie elementu zawsze zapisuje `pin … at`, niezależnie od klawisza Option. Zobacz [Rysowanie ręczne w widokach o stałym układzie](fixed-views.md).

## Zaznaczanie i przesuwanie kilku elementów

Kliknij element, aby go zaznaczyć. Kliknięcie z klawiszem Shift (lub Command na macOS, Ctrl na Windows i Linuksie) dodaje element lub linię do zaznaczenia albo usuwa obiekt z zaznaczenia. Aby zaznaczyć obszar, naciśnij przycisk myszy na pustym fragmencie płótna i przeciągnij ramkę zaznaczenia. Zaznaczone zostaną wszystkie elementy i linie mieszczące się w całości w ramce. Przytrzymaj Shift lub Command/Ctrl, aby dodać je do bieżącego zaznaczenia. Na płótnie Command-A (Ctrl-A w aplikacji na Linux oraz w aplikacji internetowej na Windows i Linuksie) zaznacza całą zawartość widoku. Escape lub kliknięcie pustego miejsca na płótnie usuwa zaznaczenie. Jeśli klikniesz jeden z kilku zaznaczonych elementów bez przeciągania, tylko on pozostanie zaznaczony.

Przeciągnij dowolny z zaznaczonych elementów, a wszystkie przesuną się razem, zachowując wzajemne położenie. Obrys każdego z nich wskazuje, gdzie trafi po upuszczeniu. Zwolnienie przycisku myszy zapisuje wiersz `pin … at` dla każdego elementu. Jest to jedna zmiana, więc jedno cofnięcie przywraca wszystkie elementy na poprzednie miejsca:

```
view main {
  include *
  layout layered direction down
  pin api at 262, 120
  pin db at 262, 246
}
```

Grupa zawsze trafia tam, gdzie ją upuścisz: Option (Alt) nie ustawia jej względem sąsiadów. W widoku z `layout fixed` linia, której oba końce się przesuwają, zachowuje kształt, ponieważ jej punkty załamania przesuwają się razem z nią. Delete (lub Backspace) usuwa wszystkie zaznaczone obiekty.

Naciśnięcie przycisku myszy wewnątrz kontenera zaznacza kontener zamiast rozpocząć rysowanie ramki. Zacznij więc zaznaczanie poza kontenerem albo klikaj jego elementy z klawiszem Shift.

## Przywracanie położenia elementu

Usuń z widoku wiersz `pin`, `align` lub `order` dotyczący elementu, a algorytm ponownie wyznaczy jego położenie.
