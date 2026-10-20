# Сборка из VS Code

Всё, что нужно для цикла «собрал → поставил на шлем → посмотрел лог», лежит в `.vscode/tasks.json`.
Открой папку репозитория в VS Code и запускай задачи через **Terminal → Run Task** (или Cmd+Shift+P → *Tasks: Run Task*).

## Один раз
1. JDK 17, Android Studio (SDK + NDK; версия NDK в `app/build.gradle.kts`), `adb` в PATH, по желанию `brew install ccache`.
2. `git clone --recurse-submodules https://github.com/elMepox/QuestZDoom.git`, затем `git checkout doomxr`.
3. Шлем: включи режим разработчика и отладку по USB, один раз подключи кабелем и разреши отладку в шлеме.
4. Задача **DoomXR: connect headset (Wi-Fi)**: найдёт шлем по USB, переключит adb на Wi-Fi и подключится.
   Адрес по умолчанию `192.168.1.60:5555`; если IP изменился, впиши новый, когда спросит. Резервация IP в роутере избавит от этого.

## Каждый день
| Что хочу | Задача |
| --- | --- |
| Собрать, поставить, запустить | **build, install and launch** (Cmd+Shift+B) |
| Только собрать | **build** |
| Посмотреть лог | **logcat** (фильтр: DoomXR, UZDoom, падения); перед запуском можно **clear logcat** |
| Приложение упало | **crash log** |
| Запущено ли | **app status** |
| Остановить | **stop app on headset** |

Шлем перезагрузился или заснул и пропал из `adb devices`: заново **connect headset (Wi-Fi)**. Если не выходит, один раз по USB.

## Тест чужой ветки или PR
1. **switch branch**: введи имя ветки (например `claude/...` из PR). Незакоммиченные правки сначала убери.
2. **from scratch**: сбросит движок `external/UZDoom`, заново применит `patches/uzdoom/*.patch`, соберёт с нуля, переустановит и запустит.
   Это то же, что делаем мы, когда проверяем PR на шлеме.
3. Вернуться: **switch branch** → `doomxr`, затем снова **reset engine and apply patches**.

## Если что-то не так
- `does not apply` при сборке: задача **reset engine and apply patches**.
- Установка ругается на подпись: **uninstall from headset**, затем снова установка.
- Странности после смены веток: **clean build** (CMake-кэш и `app/build` сносятся, ccache остаётся).
- Скорость сборки: [build-speed.md](build-speed.md).
- Мы специально не добавили `launch.json`: отладчика для нативного кода на шлеме в репозитории пока нет.
