---
name: aurora-emulator-test
description: Сборка example RPM, запуск эмулятора AuroraOS (QEMU), установка приложения и прогон интеграционных тестов на нём. Использовать при запуске example/integration_test на Aurora-эмуляторе, при ошибке сборки "Cannot connect to the Docker daemon" или когда `flutter devices` не находит aurora-устройство.
---

# Aurora-эмулятор: сборка, установка, тесты

Рунбук для этой машины. Все команды — из корня репозитория
`/Users/dmitry/aurora-projects/sqflite_sqlcipher_aurora`, всегда Aurora-SDK
Flutter — **не** системный `flutter`:

    FLUTTER=/Users/dmitry/flutter-aurora/bin/flutter

## 1. Docker (обязателен для сборки RPM)

RPM собирается в контейнере `aurora-build-tools:5.2.1.200`. Если демон не
запущен, сборка умирает так:

    Cannot connect to the Docker daemon at unix:///Users/dmitry/.docker/run/docker.sock
    Error: No rpm packages found for signing.

Лечение:

    open -a Docker
    for i in $(seq 1 30); do docker info >/dev/null 2>&1 && break; sleep 3; done

## 2. Сборка

    cd example
    $FLUTTER build aurora --release --target-platform aurora-arm64

Результат: `example/build/aurora/bt_5.2.1/aurora-arm64/release/RPMS/
com.pumano.sqflite_sqlcipher_aurora_example-0.0.0-1.aarch64.rpm`.
Строка `(ERROR) Hardcoded path found` для `libapp.so` — известное безвредное
предупреждение. Подкоманды `flutter build rpm` **нет** — она называется
`aurora` (даёт пакет `flora_tools`). Единственный авторитетный C++-компиляционный
чек — CMake/ninja в контейнере; хостовый `clang++ -fsyntax-only` ненадёжен
(разный SDK в pub-кэше хоста и в контейнере).

## 3. Эмулятор

    $FLUTTER emulators                        # список: AuroraOS-5.2.1.200, Pixel_10_Pro
    nohup $FLUTTER emulators --launch AuroraOS-5.2.1.200 > /tmp/emu_launch.log 2>&1 &

Поднимается `qemu-system-aarch64` (HFV, 4 GB, 2 vCPU) за несколько секунд, но
бут гостя не мгновенный. Поллим проксируемый SSH-порт, потом жди детекцию:

    nc -z 127.0.0.1 2223 && echo PORT-OPEN    # hostfwd 2223 -> гость :22
    $FLUTTER devices                         # ждём ... aurora-localhost:2223

Детекция **flaky** до ~минуты после бута: порт открыт и SSH работает, а
`flutter devices` говорит «No wireless devices were found» — просто повтори
команду, состояние settles. Отладка: `$FLUTTER devices -v` (печатает nc/ssh-пробы).

Ручной доступ в гость (работает, как только открыт порт):

    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no \
        -i /Users/dmitry/.ssh/aurora_emu defaultuser@localhost -p2223 '<команда>'

adb-подобный инструмент:

    ADT=/Users/dmitry/flutter-aurora/bin/cache/artifacts/adt_tool/bin/adt
    $ADT list        # #0 emulator-5.2.1.200 (default)
    $ADT status
    $ADT deploy -d emulator-5.2.1.200 <путь/k.rpm>

## 4. Установка

    $ADT deploy -d emulator-5.2.1.200 \
        example/build/aurora/bt_5.2.1/aurora-arm64/release/RPMS/com.pumano.sqflite_sqlcipher_aurora_example-0.0.0-1.aarch64.rpm

`flutter test` тоже ставит приложение (startApp = сборка + установка +
запуск), но провал любого шага выглядит как «Unable to start the app on the
device» — явный `adt deploy` позволяет изолировать, что именно сломалось.

## 5. Тесты

    cd example
    $FLUTTER test integration_test/ -d aurora-localhost:2223            # весь набор, ~3-4 мин
    $FLUTTER test integration_test/<файл>_test.dart -d aurora-localhost:2223

Id устройства — `aurora-localhost:2223`: **не** `localhost:2223` и **не**
`AuroraOS-5.2.1.200` (это id эмулятора, а не устройства). Успех —
`All tests passed!` со счётчиком `+N`; провалы — `-[N]` и `did not complete
[E]` (зависший тест крутится до длинного таймаута, ~11 минут).

## Диагностика

- «No supported devices found» → по порядку: жив ли qemu
  (`ps ax | grep qemu-system`); открыт ли порт (`nc -z 127.0.0.1 2223`);
  жив ли sshd гостя (ручной ssh из §3); затем просто повтори `$FLUTTER
  devices` — после бута детекция flaky, ретраи обычно хватает.
- Эмулятор умер → `pkill -f qemu-system-aarch64`, перезапуск по §3.
- «Unable to start the app on the device» → либо падала сборка (Docker,
  §1–2), либо приложения нет в госте: `ssh ... 'rpm -q
  com.pumano.sqflite_sqlcipher_aurora_example'`, затем переустановить (§4).
- Состояние гостя — в qcow2-оверлее
  `/Users/dmitry/AuroraOS/emulator/AuroraOS-5.2.1.200/image.qcow2` (COW над
  чистым базовым образом рядом): установленные приложения переживают
  перезапуск эмулятора, но исчезают, если оверлей пересоздан.
- `sfdk ... emulator status` печатает по-русски («выполнение: да») независимо
  от LANG — тулинг понимает оба языка, «чинить» не надо.
- Конфиги: список устройств `/Users/dmitry/devices.toml` (ключ
  `~/.ssh/aurora_emu`); ssh-ключ эмулятора
  `/Users/dmitry/AuroraOS/vmshare/ssh/private_keys/sdk`; бинарь sfdk
  `/Users/dmitry/AuroraOS/QT Creator.app/Contents/MacOS/sfdk`.
