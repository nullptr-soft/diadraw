# Siatka

Włącz siatkę, aby ręcznie wyrównać elementy. Na macOS wybierz **View ▸ Show Grid** (⌘') lub kliknij przycisk siatki na pasku narzędzi. Na Windows i Linuksie wybierz **Show Grid** w menu (Ctrl+' na Linuksie), a w aplikacji internetowej kliknij **Grid** na pasku narzędzi (⌘' lub Ctrl+'). Siatkę widać w tle diagramu. Domyślnie jej linie są rozmieszczone co 10 jednostek diagramu, a co piąta linia jest ciemniejsza. Przy dużym oddaleniu najpierw znikają jaśniejsze linie.

Gdy siatka jest widoczna, działa przyciąganie do niej na płótnie:

- **Przesuwanie elementu** umieszcza jego lewy górny narożnik w najbliższym punkcie siatki. Zapisany wiersz `pin … at` zawiera więc współrzędne tego punktu (np. `pin db at 260, 250` przy kroku siatki równym 10). Zobacz [Przesuwanie elementów](moving.md).
- **Zmiana rozmiaru elementu** w widoku z `layout fixed` przyciąga przeciągany narożnik do punktu siatki. W innych widokach algorytm określa położenie elementu i zapisywany jest tylko jego rozmiar (`size`). W tych widokach, a także w przypadku obróconego elementu, szerokość i wysokość stają się wielokrotnościami kroku siatki. Zmiana rozmiaru z klawiszem Shift zachowuje proporcje, więc nie korzysta z przyciągania. Zobacz [Zmiana rozmiaru i obracanie](resize-and-rotate.md).
- **Zaginanie linii** w widoku o stałym układzie umieszcza punkt załamania w punkcie siatki.

Siatka nie wpływa na przeciąganie z klawiszem Option (Alt na Windows i Linuksie): algorytm ustawia element względem jego sąsiadów. W widoku o stałym układzie przeciąganie z Option nadal zapisuje `pin … at`, ale umieszcza element dokładnie tam, gdzie go upuścisz, nawet poza siatką. Obracanie elementu zachowuje własny krok 15°.

Siatka jest ustawieniem aplikacji, nie rysunku: jej przełączenie nie zapisuje niczego w tekście, a wybrane ustawienie obowiązuje w każdym otwieranym dokumencie. Na macOS w menu **View ▸ Grid Size** możesz wybrać krok 5, 10, 20 lub 40 jednostek. Wybór kroku włącza siatkę i jest zapamiętywany. Tak samo działa lista obok przycisku **Grid** w aplikacji internetowej, która zapamiętuje wybór w przeglądarce. Wyłączenie siatki niczego nie przesuwa. Przy następnym przeciągnięciu element znów trafi dokładnie tam, gdzie go upuścisz.
