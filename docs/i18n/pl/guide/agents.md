# Rysowanie z agentami AI

Diagramy DiaDraw to zwykły tekst, więc agent AI pisze je równie łatwo jak kod. Agent zapisuje plik `.draw`, sprawdza go, renderuje, ogląda obraz i poprawia to, co wygląda źle, czyli przechodzi tę samą pętlę co człowiek w aplikacji. Ułatwiają to dwa elementy, które dobrze ze sobą współpracują:

- **Serwer MCP**, `diadraw mcp`: daje każdemu agentowi obsługującemu [Model Context Protocol](https://modelcontextprotocol.io) (Claude Code, Claude Desktop i inne) narzędzia do sprawdzania, renderowania i importowania diagramów. Renderowanie zwraca obraz, więc agent widzi narysowany przez siebie diagram.
- **Umiejętność (skill) dla Claude Code**: uczy Claude Code języka oraz pętli sprawdź, wyrenderuj, obejrzyj i popraw. Zainstalujesz ją poleceniami `/plugin marketplace add mykulyak/diadraw` i `/plugin install diadraw@diadraw` (zobacz [plugins/diadraw/README.md](../../plugins/diadraw/README.md)).

## Co może agent

| Narzędzie   | Co robi                                                                                                                       |
| ----------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `check`     | Zgłasza błędy i ostrzeżenia jako `wiersz:kolumna: ważność: komunikat` i wymienia widoki dokumentu                             |
| `render`    | Rysuje jeden widok i zwraca go jako obraz PNG (lub SVG); może też zapisać plik `.svg`, `.png` lub `.pdf`                      |
| `format`    | Zwraca dokument w postaci kanonicznej albo przepisuje plik                                                                    |
| `import`    | Przekształca diagramy Mermaid, PlantUML, Structurizr, draw.io i BPMN w dokument DiaDraw                                       |
| `reference` | Wymienia rodzaje elementów, kształty, motywy, układy i biblioteki albo rodzaje i kształty jednej biblioteki (`library: "c4"`) |
| `examples`  | Wymienia [przykładowe diagramy](examples.md) i zwraca dowolny z nich, aby agent mógł zacząć od podobnego                      |

Każde narzędzie przyjmuje dokument jako `source` (jego tekst) albo `path` (plik). Lepiej używać plików w projekcie: diagram trafia wtedy do systemu kontroli wersji jak wszystko inne. Biblioteki są wyszukiwane offline, tak jak w wierszu poleceń: wbudowane, zestawy szablonów draw.io i to, co `diadraw lib install` umieściło w pamięci podręcznej.

## Konfiguracja

Serwer jest częścią narzędzia wiersza poleceń `diadraw`. Zawierają go pakiety dla Linuksa i Windows; w innych przypadkach zbuduj go z repozytorium (zobacz [src/README.md](../../src/README.md)):

```sh
cmake --preset release && cmake --build --preset release
# plik wykonywalny to ../diadraw-build/release/diadraw
```

Następnie wskaż agentowi, gdzie się znajduje. Jeśli nie ma go w `PATH`, podaj pełną ścieżkę.

### Claude Code

```sh
claude mcp add diadraw -- /ścieżka/do/diadraw mcp
```

Serwer zostanie dodany do bieżącego projektu tylko dla Ciebie. Dodaj `--scope user`, aby mieć go w każdym projekcie, albo `--scope project`, aby zapisać go w pliku `.mcp.json` projektu i udostępnić wszystkim, którzy nad nim pracują. W Claude Code wpisz `/mcp`, aby sprawdzić, czy połączenie działa.

### Claude Desktop

Otwórz **Settings › Developer › Edit Config** i dodaj DiaDraw do pliku `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "diadraw": {
      "command": "/ścieżka/do/diadraw",
      "args": ["mcp"]
    }
  }
}
```

Uruchom ponownie Claude Desktop. Claude Desktop uruchamia serwer poza jakimkolwiek projektem, więc poproś, aby podawał pełne ścieżki plików, które czyta i zapisuje.

### Inni agenci

Każdy klient MCP może uruchomić `diadraw mcp` jako lokalny serwer (stdio): czyta jeden komunikat JSON-RPC na wiersz ze standardowego wejścia i odpowiada na standardowym wyjściu.

## Jak poprosić o diagram

Poproś zwykłymi słowami i powiedz, gdzie ma trafić plik, na przykład:

> Narysuj kontenery naszej usługi zamówień jako diagram C4 w `docs/checkout.draw`, wyrenderuj go do `docs/checkout.svg` i sprawdź, czy obraz jest czytelny.

Agent zapisuje plik, uruchamia `check`, dopóki nie znikną błędy, a potem wywołuje `render` i ogląda obraz. Jeśli prostokąty na siebie zachodzą albo przepływ biegnie w złą stronę, zmienia widok, na przykład `layout layered direction right` lub `pin db below api`, i renderuje ponownie. Ten sam plik możesz potem otworzyć w dowolnej aplikacji DiaDraw i dalej edytować ręcznie lub na kanwie.
