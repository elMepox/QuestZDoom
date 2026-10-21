# DoomXR

Современный VR-порт [UZDoom](https://github.com/UZDoom/UZDoom) (наследник GZDoom) для Meta Quest 3 / 3S на OpenXR.
Проект вырос из форка [QuestZDoom](https://www.questzdoom.com) (DrBeef): движок, зависимости и графический API
подняты до современных, а VR-слой переписан заново. Ветка `doomxr` — основная, `master` хранит старый QuestZDoom.

Это только движок: игровых данных (IWAD) в нём нет. Для запуска без покупки есть встроенный Freedoom, а свои
`doom.wad` / `doom2.wad` и т.д. кладутся в папку приложения.

## Что умеет

- **VR-ядро на OpenXR.** Стерео-рендер на гарнитуру, трекинг головы и контроллеров, рендер на частоте
  шлема даже на паузе и в системном меню.
- **Прицел и лучи.** Оружие смотрит туда, куда направлен контроллер; лазерный луч показывает точку попадания.
- **Две руки.** Двуручное удержание оружия и режим dual wield (по оружию в каждой руке) с отдельными лучами.
- **HUD на предплечье.** Здоровье, броня, патроны и выбор оружия на левом предплечье, как наручные часы;
  панелью можно управлять лучом/касанием.
- **3D-модель оружия в руке.** Если у оружия есть запись MODELDEF, оно рисуется как 3D-модель в позе контроллера
  (см. [docs/hand-held-models.md](docs/hand-held-models.md)); остальное остаётся спрайтами.
- **VR-настройки.** Меню VR settings для лучей, dual wield, HUD и параметров моделей; те же параметры доступны
  как консольные переменные `vr_*`.
- **Менеджер модов и каталог.** Стартовый экран-библиотека: выбор IWAD и модов, порядок загрузки, запоминание
  выбора между запусками, каталог проверенных модов ([catalog/](catalog/README.md)) и поиск по архиву idgames
  с загрузкой и проверкой sha256.

Пока не готово: 3D-руки (общий слой рук) — в работе, в релиз не входит.

## Сборка и запуск

Нужны JDK 17, Android SDK + NDK (версия в `app/build.gradle.kts`), `adb` и, для ускорения, `ccache`.

```sh
git clone --recurse-submodules https://github.com/elMepox/QuestZDoom.git
cd QuestZDoom && git checkout doomxr && git submodule update --init --recursive
./gradlew assembleDebug        # сам применит patches/uzdoom/*.patch к external/UZDoom
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n io.github.elmepox.doomxr/.LibraryActivity
```

В VS Code те же шаги доступны задачами (`.vscode/tasks.json`, Cmd+Shift+B): подключение к Quest по Wi-Fi,
сборка, установка, запуск, logcat, смена ветки, сброс движка. Подробно: [docs/vscode.md](docs/vscode.md).
Подробности про скорость сборки — [docs/build-speed.md](docs/build-speed.md).

## Устройство репозитория

| Путь | Что там |
| --- | --- |
| `external/UZDoom`, `SDL`, `openal-soft` | подмодули; UZDoom не форкается, а патчится |
| `patches/uzdoom/` | пронумерованные патчи движка: OpenXR, контроллеры, прицел и лучи, dual wield, HUD, модели |
| `app/` | Android-приложение: `LibraryActivity` (библиотека модов), `DoomXRActivity`, OpenXR-слой `cpp/xr` |
| `modcore/` | платформенно-независимая логика модов (каталог, idgames, загрузка, IWAD, профиль запуска) с тестами |
| `catalog/` | каталог модов, который приложение скачивает при старте |
| `testmods/` | тестовые паки для проверок в шлеме |
| `scripts/` | применение патчей, замер сборки, проверка ссылок каталога, загрузка Freedoom |

## Предупреждение

VR может вызывать укачивание. При первых признаках остановитесь и отдохните; для передвижения есть телепорт.

## Благодарности

- [Команды ZDoom и UZDoom](https://zdoom.org/index) за движок.
- DrBeef и все, кто делал QuestZDoom, на котором основан этот проект; [Emile Belanger](http://www.beloko.com/)
  за Android-сборку LZDoom, а также участники GZ3Doom, откуда взяты идеи 6DoF-оружия.
- Baggyg (QuestZDoom Launcher), VR_Bummser, Daniel Teich и закрытые бета-тестеры оригинального порта.
- m8f (mmaulwurff) — [Laser Sight](https://github.com/mmaulwurff/laser-sight), вдохновивший прицельные лучи.
- Chris Collins — GeneralUserGS Soundfont.
- FreeDoom / Fraggle — за Freedoom.

## Дисклеймер

DoomXR не связан с id Software, Bethesda и их аффилированными компаниями, не одобрен ими. Защищённых авторским
правом игровых данных в приложении нет. Лицензия — см. [LICENSE](LICENSE).
