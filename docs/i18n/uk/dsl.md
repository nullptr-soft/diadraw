# DSL DiaDraw

Для всіх типів діаграм діє одна граматика: у блоці `model` у стилі Structurizr об’єкти визначаються один раз, а блоки `view` вибирають і впорядковують їх та задають обмеження. Файли з розширенням `.draw` є першоджерелом, тож зміни діаграм зручно порівнювати й рецензувати в git.

Граматику задано у файлі [`diadraw.grammar`](diadraw.grammar) в нотації Lezer як довідковий опис; його ніщо не компілює. Синтаксичний аналізатор ядра (`src/dsl/src/syntax.cpp`) написано вручну методом рекурсивного спуску. Він стійкий до помилок і будує описане граматикою дерево з тими самими назвами вузлів. `parse(source)` (`src/dsl/src/lower.cpp`) перетворює синтаксичне дерево на операції над моделлю та повертає модель, діагностичні повідомлення з рядком і стовпцем і карту відповідності вихідному тексту. `print(model)` записує канонічний текст; кожен файл тестового корпусу проходить цикл виведення → розбір без змін.

Це довідник. Про призначення функцій і те, коли ними користуватися, читайте в [посібнику користувача](guide/README.md).

## Приклад

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

## Діаграми довільної форми

Геометрія намальованих вручну (або імпортованих із draw.io) діаграм зберігається в тексті:

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

- `style "k=v;k=v"` в елементі чи зв’язку задає його вигляд. Ключі елемента: `fill`, `stroke`, `strokeWidth`, `dashed`, `dashPattern`, `rounded`, `opacity` (0–100), `fontColor`, `fontSize`, `fontStyle` (жирний — 1, курсив — 2, підкреслення — 4), `align`, `verticalAlign`, `labelPosition`, `verticalLabelPosition`. Ключі зв’язку: `stroke`, `strokeWidth`, `dashed`, `dashPattern` (за `dashed=1`: `1 3` — крапки, `8 3 1 3` — штрих і крапка тощо), `double` (дві лінії), `curved`, `rounded` (заокруглені кути), `edgeStyle`, `startArrow`, `endArrow`, `startFill`, `endFill`. Для закінчень використовуються назви з draw.io: `classic`, `block`, `open`, `async`, `openAsync`, `doubleBlock`, `diamond`, `oval`, `box`, `circlePlus`, `halfCircle`, `baseDash`, `dash`, `cross`, `none` і закінчення типу «вороняча лапка» `ERone`, `ERmandOne`, `ERzeroToOne`, `ERmany`, `ERoneToMany`, `ERzeroToMany`; `startFill=0`/`endFill=0` залишає фігуру незаповненою. Елемент із таким стилем малюється як у draw.io (типово біла заливка й чорний контур), а контейнер — власною фігурою замість штрихової рамки. Невідомі ключі зберігаються.
- `prop "key" "value"` зберігає довільну властивість (можна повторювати).
- `shape` приймає базову фігуру: `rect rounded ellipse circle rhombus triangle hexagon octagon cylinder
parallelogram trapezoid document cloud actor person note card step arrow process stadium cube
swimlane folder queue interface text image invisible frame module lifeline`.
- `layout fixed` точно зберігає кожну вказівку `pin … at X, Y size W, H` (незакріплені вузли вишиковуються нижче); `route "relationship id" via X, Y … from EX, EY to NX, NY` фіксує точки згину ребра та точки, де воно виходить із початкового й входить до кінцевого блока (відносно блока, від 0 до 1). Без `from`/`to` ребро виходить у напрямку першої точки згину. На полотні фіксованого подання переміщення вузла чи зміна його розміру переписує `pin … at … size`, а перетягування точок згину — `route` (див. [Малювання вручну у фіксованих поданнях](guide/fixed-views.md)).

## Стилі й теми

`theme` вибирає кольори для відображення документа, а блоки `style` задають спільний вигляд усіх елементів певного виду, усіх об’єктів із певним тегом або всіх зв’язків:

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

- Теми: `default`, `dark` (темне тло) і `mono` (чорне на білому, сіра заливка, для друку). `theme` у поданні має перевагу над темою документа; невідоме ім’я спричиняє попередження, і використовується `default`. Без інструкції `theme` усі подання, як і раніше, використовують `default`.
- Селектори: `*` (кожен елемент), вид (`container`), `tag "…"`, вид і тег (`container tag "legacy"`), `relationship` і `relationship tag "…"`. У селекторах `relationship` і `tag` є ключовими словами, тож вибрати вид із такою назвою неможливо.
- Кожен рядок блока має вигляд `key value`. Ключі ті самі, що й для вбудованого `style "…"` (перелічені вище; невідомі зберігаються). Значення — рядок, число або слово; `true` і `false` зберігаються як `1` і `0`, як у draw.io.
- Пріоритет від найнижчого: тема, блоки `*`, блоки виду, блоки тегу, блоки виду й тегу (у межах одного рівня пізніші блоки мають перевагу), потім власний вбудований `style "…"` об’єкта.
- Блок `style` змінює лише те, що задає: елемент зберігає інші кольори теми й жирний підпис. Вбудований `style "…"`, як і раніше, застосовує до незаданих властивостей вигляд draw.io (біла заливка, чорний контур). У видів із дочірніми елементами блоки змінюють оформлення рамки (`fill`, `stroke`, `strokeWidth`, `dashed`, `fontColor`).
- `style` і `theme` — інструкції верхнього рівня; `theme` також може стояти в поданні. Усередині `model { … }` вони залишаються звичайними іменами, а `style "…"` в елементі — вбудованим стилем.
- `print` записує спочатку оголошення тем, потім вибрану тему, а далі блоки за порядком, по одній парі `key value` на рядок.

### Власні теми

Документ оголошує тему з тілом і вибирає її так само, як вбудовану:

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

- `extends` указує на вбудовану, іншу оголошену або бібліотечну тему. Без нього за основу береться `default`. Кожна тема змінює лише те, що задає.
- Ключі теми: `background`, `fontFamily`, `fontSize`, `smallFontSize`, `edge` та `edgeText` (лінії й підписи зв’язків), `boundary` та `boundaryText` (рамки контейнерів і груп), `surface` (активації, порожнисті наконечники стрілок), `dividerFill`, `labelText` (підписи під піктограмами).
- `node <kind> { fill … stroke … text … }` задає кольори виду; `node default` стосується всіх видів без власного запису, `node external` — елементів із тегом `external`.
- Бібліотеки також постачають теми (див. [бібліотеки](libraries.md#маніфест)): після `use "acme-brand@^1" as acme` інструкція `theme acme.corporate` вибирає тему, а `theme mine extends acme.corporate { … }` створює власну на її основі.
- Діагностика повідомляє про невідомі ключі й цілі `extends`, повторення імен, використання вбудованих імен і цикли `extends`; якщо тему не вдається визначити, використовується `default`.

## Подання послідовності

Подання з `layout sequence` малює включені елементи як лінії життя в порядку `include`, а кроки блока `sequence` — згори вниз:

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

Рамки бувають `alt`, `opt`, `loop`, `par`, `critical`, `break` і `group`; кожна може мати секції `else "label" { … }`. Повідомлення належать поданню, а не моделі: ті самі учасники можуть з’являтися в багатьох поданнях послідовності, тоді як зв’язки моделі описують лише загальну картину.

## Примітки та оформлення

Примітки, довільний текст і рамки належать одному поданню, а не моделі. Вони пояснюють діаграму, не додаючи елементів, тож інші подання тієї самої моделі залишаються без зайвих деталей.

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

| Об’єкт | Розміщення |
| --- | --- |
| `note [name] "text" [anchor] [style "…"]` | наліпка; після прикріплення до елемента сполучається з ним штриховою лінією |
| `text [name] "text" [anchor] [style "…"]` | довільний текст, заголовок або підпис |
| `box [name] ["label"] around a b … [style "…"]` | штрихова заокруглена рамка навколо блоків елементів, із підписом угорі ліворуч |
| `box [name] ["label"] at X, Y size W, H [style "…"]` | рамка з фіксованими координатами |

Прив’язка задається через `below`, `above`, `left of` чи `right of` відносно елемента подання або `at X, Y` з необов’язковим `size W, H`. Без прив’язки текст розміщується над діаграмою, а примітки — у стовпчик праворуч. Оформлення додається після компонування, тому не зсуває елементи: якщо об’єкт перекрив би вузол, підпис ребра чи іншу примітку, його відсувають далі від елемента прив’язки. Текст примітки переноситься на ширині 200 пікселів.

- Ім’я необов’язкове й має бути унікальним у межах подання; за ним до об’єкта можуть звертатися редактор і середовища виконання (SVG містить його в `data-name`, а кожна прикраса має `data-decoration="<index>"` і клас `dd-decoration dd-note|dd-text|dd-box`).
- `style` приймає ключі елемента: `fill`, `stroke`, `strokeWidth`, `dashed` (рамки штрихові, якщо не задано `dashed=0`), `rounded`, `opacity`, `fontColor`, `fontSize`, `fontStyle`, `align`, `verticalAlign`.
- Видалення елемента видаляє прикріплене до нього оформлення та рамки навколо нього.
- `note`, `text`, `box` і `around` — контекстні ключові слова: елементи й далі можуть так називатися. Усередині `sequence { … }` вирази `note over …` / `note left of …` зберігають значення приміток послідовності.

## Лексичні правила

- Пробільні символи несуттєві; роздільники інструкцій не потрібні.
- Коментарі: `// to end of line` та `/* block */`. Обидва зберігаються в синтаксичному дереві.
- Ідентифікатори: `[A-Za-z_][A-Za-z0-9_]*`. Посилання на вкладені елементи використовують крапки: `shop.web`.
- Рядки: в подвійних лапках, з екрануванням через `\` (`\n` — перенесення рядка), в один рядок. **Блоковий рядок** займає кілька рядків: він відкривається `"""` і закінчується наступним `"""`, з тим самим екрануванням. Залишок рядка після відкривального `"""` і рядок закривального `"""` відкидаються, якщо вони порожні, як і відступ, спільний для решти рядків, тож текст можна відступати разом з інструкцією навколо. Порожні рядки всередині зберігаються:

  ```
  db = container "Orders DB" description """
    Orders and their lines.

    Archived after two years.
  """
  ```

  Блокові рядки можна писати всюди, де й звичайні. `format` записує багаторядковий `description` блоковим рядком, а решту рядків — з `\n`.
- Числа: `-?digits(.digits)?`.
- Ключові слова контекстні: `use as model view of group port north south east west tech shape tag
description style prop include exclude layout direction spacing right left down up pin below
above at size route via from to align horizontally vertically order sequence activate
deactivate note over divider delay ref autonumber alt opt loop par critical break else theme relationship extends node
text box around` є ключовими словами там, де цього вимагає граматика, і звичайними іменами в інших місцях (`layout = container "Layout"` допустимо). Якщо можливі обидва тлумачення, як у списку селекторів, за яким іде `layout …`, перевагу має ключове слово.

## Неформальна граматика

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

## Семантика

| Синтаксис | Модель |
| --- | --- |
| `web = container "Web app"` у `shop` | елемент `shop.web`, вид `container`, назва `Web app`, батьківський елемент `shop` |
| без підпису | типовою назвою є ідентифікатор |
| `tech`, `shape`, `tag`, `description` | однойменні поля елемента / зв’язку |
| `style "k=v;…"`, `prop "k" "v"` | стиль і властивості елемента чи зв’язку |
| `a -> b "label"` | зв’язок з ідентифікатором `a->b` (`a->b#2`, `#3`, … для повторень) |
| `pay = a -> b` | зв’язок із явним ідентифікатором `pay` |
| `a.http -> b` | якщо `a` має `port http`, кінцевою точкою є цей порт |
| `group "Build" { … }` | група; для вкладених елементів задається `group` |
| `view v of shop` | подання, область якого обмежена дочірніми елементами `shop` |
| `include *` | кожен елемент в області видимості; подання без `include` включає `*` |
| `layout …` | алгоритм подання, напрямок та інтервал (типово `layered`) |
| `pin a below b` | `PinHint` із розміщенням `Below` |
| `pin a at 10, 20 size 120, 60` | `PinAtHint` із позицією й необов’язковим розміром (явний вибір довільної геометрії) |
| `route "a->b" via 1, 2 from … to …` | `RouteHint`: точки згину, вихід і вхід (0–1 від розміру блока) |
| `size a 200, 80` | `SizeHint`: розмір елемента в цьому поданні, зберігається за будь-якого компонування |
| `rotate a 30` | `RotateHint`: поворот на 30° за годинниковою стрілкою навколо центра (лише кінцеві вузли) |
| `align a b horizontally` | `AlignHint` (типово за горизонтальною віссю) |
| `order a b c` | `OrderHint` |
| `sequence { … }` | кроки послідовності подання; `autonumber` у будь-якому місці вмикає нумерацію |
| `theme dark` | `model.theme` (у поданні: `view.theme`) |
| `theme brand extends dark { … }` | `model.themes[]`: `{ name, extends?, …keys, nodes? }` |
| `style container { fill "#438dd5" }` | `model.styles[]`: `{ target: 'elements', kind, tag?, style }` |
| `note n "x" right of a` | оформлення `{ kind: 'note', name: 'n', placement: 'right-of' }` |
| `box "x" around a b` | оформлення `{ kind: 'box', around: [a, b] }` |

Види елементів задаються довільними ідентифікаторами. Вбудовані види: `person`, `system`, `container`, `component`, `database`, `process`, `decision`, `start`, `end`, `note`, `class`, `object`, `event`, `task`, `subprocess`, `gateway`, `data`, `annotation`, `group`, `pool`, `lane` та `conversation`. Інші види приймаються з попередженням; бібліотеки додають власні (див. нижче). `person` зображується як схематична фігурка людини з іменем під нею в кольорі `labelText` теми, оскільки ім’я розташовується на полотні (`fontColor` стилю має перевагу, а вбудований `style` зберігає чорний колір draw.io). Заливка визначає колір голови, а `node person { text }` теми задає колір імені лише тоді, коли `shape` зображує людину як блок із заливкою.

Алгоритми компонування: `layered` (типовий), `tree`, `grid`, `radial`, `force`, `fixed` та `sequence` (призначення кожного описано в [посібнику користувача](guide/layouts.md)). Інші назви зберігаються, але запускають пошаровий алгоритм із попередженням на рядку `layout`.

## Види елементів із бібліотек

Бібліотека може додавати види елементів (`kinds` у маніфесті, див. [libraries.md](libraries.md#види-елементів)). У документі до них звертаються через псевдонім бібліотеки, як і до її фігур:

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

- Елемент бібліотечного виду зображується фігурою цього виду (бібліотечний рисунок або базова фігура) в його кольорах, якщо не задає власних `shape` чи `style`. Якщо він має дочірні елементи, то, як і види C4, зображується рамкою з підписом зі стереотипом виду (`Jobs [queue]`).
- Вид зберігається так, як написано, наприклад `acme.topic`, і в моделі, і у виведеному тексті.
- Якщо псевдонім виду не оголошено через `use`, під час розбору видається попередження. Якщо бібліотека не містить виду, `diadraw check`, `diadraw render` і редактор видають попередження, як і для невідомої фігури, а елемент малюється як блок у типових кольорах. В обох випадках документ усе одно перетворюється на модель.
- Бібліотечні види завжди мають кваліфіковані імена, тому не конфліктують ані з вбудованими видами, ані між собою.

## Посилання

Ідентифікатор зв’язку утворюється з його кінцевих точок, якщо не задано ім’я. Похідний ідентифікатор змінюється разом із ребром: перенесення кінця змінює ідентифікатор, а видалення першого з двох паралельних ребер перетворює `a->b#2` на `a->b`. Тож під час ручного редагування тексту вказівка `route` зі старим ідентифікатором втрачає прив’язку (під час перепідключення на полотні такі маршрути переписуються з новими ідентифікаторами). Якщо назвати ребро, `pay = a -> b`, воно назавжди отримає ідентифікатор `pay`: зміна кінців чи порядку оголошень не вплине на нього й `route "pay" …` діятиме й надалі (про те, коли називати ребро, див. [Ідентифікатори зв’язків](guide/relationships.md#ідентифікатори-звязків)). Ім’я використовується без змін незалежно від місця оголошення (без префікса зі шляху батьківського елемента) і має бути унікальним серед зв’язків, але може збігатися з іменем елемента. Ім’я одного ребра не зсуває похідні ідентифікатори інших. `print` виводить іменований ідентифікатор як `name = …`; ідентифікатори, які не є допустимими іменами (наприклад, з JSON), вивести неможливо, тож після розбору вони знову будуть похідними.

Посилання з крапками розв’язується від найглибшої області назовні: усередині `shop { … }` ім’я `web` означає `shop.web`, а `shop.web` працює всюди. Селектори та вказівки подання так само розв’язуються в його області видимості, тому `view checkout of shop { pin db below web }` є коректним. Посилання, остання частина якого є портом попереднього елемента (`api.http`), указує на цей порт.

## Діагностика

Розбір ніколи не генерує винятків. Аналізатор сам відновлюється після синтаксичних помилок: позначає нерозібрану інструкцію та пропускає вхідні дані до кінця її блока чи наступного рядка, який починає допустиму в цьому місці інструкцію. Завдяки цьому решта файла перетворюється на модель. Про пропущені дані повідомляє `Unexpected "…"`, а про обірвану інструкцію — `Syntax error: incomplete statement`.

Помилки моделі виявляються під час операцій ядра та розв’язання посилань і прив’язуються до відповідного фрагмента тексту: невідомі елементи, невідомі зв’язки в `route`, повторювані ідентифікатори, порти поза елементом або оголошені двічі, неправильні імена `use` чи повторювані псевдоніми. Попередження: невідомі види елементів, бібліотечні види без псевдоніма, оголошеного через `use`, і `shape` чи `description` у зв’язку (ігноруються). Рядки та стовпці нумеруються від 1; стовпці рахуються в кодових одиницях UTF-16, як у редакторах. `diadraw check file.draw` виводить повідомлення як `file:line:column: error|warning: message`.

## Правила взаємного перетворення

1. **Розбір без втрат.** Дерево зберігає коментарі та пробільні символи.
2. **Семантичні правки стають точковими змінами тексту.** Додавання, перейменування чи перепідключення на полотні змінює найменший потрібний фрагмент тексту в належному місці. Перепідключення переписує лише переміщений кінець, використовуючи найкоротше посилання, яке вказує на новий елемент із місця оголошення зв’язку, а також усі `route`, ідентифікатор зв’язку в яких змінився (іменований зв’язок зберігає ідентифікатор; похідні обчислюються заново, тому паралельні ребра можуть змінити номери; див. [посібник користувача](guide/relationships.md#перепідключення-лінії)). Редагування властивостей елемента в інспекторі (вид, ім’я, `tech`, `shape`, `tag`, `description`, `style`, `prop`) переписує лише змінені частини оголошення: змінене значення замінюється на місці, очищене — видаляється, а нове додається після останньої частини оголошення. Коментарі, порядок частин і тіло елемента залишаються незмінними (див. [посібник користувача](guide/element-properties.md)). Підпис, `tech`, стрілка, `tag`, `style` і `prop` зв’язку редагуються так само. Нове ім’я записується як `name = ` перед рядком (або цей префікс видаляється разом з іменем), і всі `route`, які вказують на змінений ідентифікатор, включно з перенумерованими похідними, отримують новий ідентифікатор. Редагування блоків стилів і тем із полотна працює так само, запис за записом: змінене значення переписується на місці, запис видаленого ключа зникає, новий ключ додається в кінець блока, а нові блоки та інструкції тем розміщуються поруч з іншими свого типу (див. [посібник](guide/styles-and-themes.md)).
3. **Геометричні правки стають вказівками, а не координатами.** Перетягування записує `pin`, `align` або `order`.
4. **Об’єкти довільної форми теж зберігаються в тексті.** Примітки, текст і рамки — це об’єкти рівня подання (`note`, `text`, `box`; див. [Примітки та оформлення](#примітки-та-оформлення)).
5. **Імпорт, а не сумісність синтаксису.** PlantUML, Mermaid, Structurizr і draw.io імпортуються в модель; їхній синтаксис не розбирається як рідний.

## Відкриті питання

Незавершені завдання щодо DSL і його редагування з полотна відстежуються в [переліку завдань](backlog.md#dsl).
