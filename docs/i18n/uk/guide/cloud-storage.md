# Google Drive і OneDrive

DiaDraw відкриває й зберігає діаграми в Google Drive і OneDrive у кожному застосунку: у вебзастосунку та в застосунках для macOS, Linux і Windows. Діаграма, збережена в одному застосунку, відкривається в інших, бо всі вони використовують те саме місце в кожному сервісі.

## Як цим користуватися

- **Вебзастосунок:** кнопка **Storage…** на панелі інструментів відкриває вікно з вкладкою для кожного місця, де може зберігатися діаграма: **This browser** (власне сховище браузера, як і раніше), **Google Drive** і **OneDrive**.
- **macOS, Linux і Windows:** команда **File › Cloud Storage…** відкриває те саме вікно з вкладками Google Drive і OneDrive (ці застосунки зберігають діаграми у файлах, тож вкладки браузера в них немає).

На вкладці хмарного сервісу кнопка **Sign in** відкриває сторінку входу цього сервісу (у вебзастосунку — у спливному вікні, у настільних застосунках — у вашому браузері). Після входу вкладка показує ваші діаграми в цьому сервісі, від найновішої. Зі списку можна:

- **Відкрити** діаграму (**Open**). У вебзастосунку вона замінює ту, що редагується; у настільних застосунках відкривається в новому вікні, якщо поточне вікно не є незайманим новим документом.
- **Зберегти поточну діаграму** під новою назвою (**Save current diagram**). Відтоді ⌘S / Ctrl+S зберігає її назад у той самий файл у цьому сервісі, а заголовок показує, де вона лежить, наприклад «Checkout (OneDrive)».
- **Перейменувати** (**Rename**) і **видалити** (**Delete**) діаграму. Видалення переміщує її до кошика Google Drive або OneDrive, звідки її можна відновити.

Якщо під час збереження термін входу минув, вікно відкривається на цьому сервісі й просить увійти знову; після цього збережіть ще раз.

## Куди потрапляють діаграми і що бачить DiaDraw

DiaDraw просить у кожного сервісу найменший доступ, який той пропонує:

| Сервіс       | Доступ, який просить DiaDraw                                          | Де зберігаються діаграми                 |
| ------------ | --------------------------------------------------------------------- | ---------------------------------------- |
| Google Drive | `drive.file`: лише файли, які DiaDraw сам створив або відкрив         | `<назва>.draw` у корені «Мого диска»     |
| OneDrive     | `Files.ReadWrite.AppFolder`: лише власна тека DiaDraw на вашому диску | `<назва>.draw` у теці **Apps › DiaDraw** |

Тож DiaDraw не може читати інші ваші файли. В OneDrive файли `.draw`, які ви самі скопіюєте до **Apps › DiaDraw**, теж з'являться в DiaDraw; у Google Drive видно лише файли, збережені DiaDraw.

Настільні застосунки тримають вхід у пам'яті, доки їх не закрито. Вебзастосунок зберігає його для вкладки браузера (у її сховищі сеансу), тож після перезавантаження сторінки ви лишаєтеся в системі, а коли вкладку закрито, вхід забувається.

## Налаштування для власної збірки DiaDraw

Кожен сервіс працює лише із зареєстрованими в ньому застосунками, тож той, хто збирає й випускає DiaDraw, один раз реєструє його в Google і Microsoft та вбудовує ідентифікатори у збірку. Збірка без них показує на вкладці «not set up» замість кнопки Sign in. Ідентифікатори не є секретними.

### Google Drive

У [консолі Google Cloud](https://console.cloud.google.com/):

1. Створіть проєкт (наприклад, «DiaDraw»).
2. **APIs & Services › Library:** увімкніть **Google Drive API**.
3. **Google Auth Platform › Branding:** назва застосунку, адреса підтримки й домашня сторінка (адреса сайту). **Audience:** External; поки застосунок у режимі «Testing», увійти можуть лише перелічені там тестові користувачі, тож опублікуйте його, коли він має працювати для всіх.
4. **Data Access:** додайте область доступу `https://www.googleapis.com/auth/drive.file`. Це нечутлива область, тож застосунку не потрібна перевірка безпеки від Google.
5. **Clients › Create client › Web application** (для вебзастосунку):
   - Authorised JavaScript origins: origin сайту, наприклад `https://mykulyak.github.io`, і `http://localhost:5173` для `npm run dev:web`.
   - Authorised redirect URIs: файл `oauth.html` застосунку, наприклад `https://mykulyak.github.io/diadraw/app/oauth.html`, і `http://localhost:5173/oauth.html`.
   - Його **client ID** — це `VITE_GOOGLE_CLIENT_ID` нижче.
6. **Clients › Create client › Desktop app** (для застосунків macOS, Linux і Windows). Його **client ID** і **client secret** — це `DIADRAW_GOOGLE_CLIENT_ID` і `DIADRAW_GOOGLE_CLIENT_SECRET` нижче. (Google видає секрет навіть для настільних застосунків; він потрапляє в кожну копію, тож не є секретним, а вхід натомість захищає PKCE.)

### OneDrive

У [центрі адміністрування Microsoft Entra](https://entra.microsoft.com/), **App registrations › New registration**:

1. Назва: «DiaDraw». Supported account types: **Accounts in any organizational directory and personal Microsoft accounts**.
2. **Authentication › Add a platform › Single-page application** (для вебзастосунку), з адресами перенаправлення `https://mykulyak.github.io/diadraw/app/oauth.html` і `http://localhost:5173/oauth.html`.
3. **Authentication › Add a platform › Mobile and desktop applications** (для настільних застосунків), із власною адресою перенаправлення `http://localhost`. (На `localhost` приймається будь-який порт.)
4. **API permissions › Add a permission › Microsoft Graph › Delegated:** `Files.ReadWrite.AppFolder` і `offline_access`.
5. **Application (client) ID** на сторінці огляду — це водночас `VITE_ONEDRIVE_CLIENT_ID` і `DIADRAW_ONEDRIVE_CLIENT_ID` нижче. Секрету немає.

### Вбудовування ідентифікаторів

- **Вебзастосунок:** задайте `VITE_GOOGLE_CLIENT_ID` і `VITE_ONEDRIVE_CLIENT_ID` у середовищі збірки (або в `src/apps/web/.env.local` для `npm run dev:web`). Workflow GitHub Pages бере їх зі змінних репозиторію `DIADRAW_GOOGLE_CLIENT_ID` і `DIADRAW_ONEDRIVE_CLIENT_ID` (**Settings › Secrets and variables › Actions › Variables**).
- **Застосунки для macOS, Linux і Windows:** передайте їх до CMake:

  ```bash
  cmake --preset release -DDIADRAW_GOOGLE_CLIENT_ID=… -DDIADRAW_GOOGLE_CLIENT_SECRET=… \
                         -DDIADRAW_ONEDRIVE_CLIENT_ID=…
  ```

  Змінні середовища з тими самими назвами перекривають їх під час запуску застосунку, що зручно, щоб випробувати реєстрацію застосунку без повторної збірки.
