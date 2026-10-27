# sqflite_sqlcipher_aurora — чеклист исправлений

Каждый пункт — отдельная ветка от `main` (`fix/<name>`), с интеграционным
тестом в `example/integration_test/`, гоняется на Aurora-эмуляторе:

```sh
cd example
/Users/dmitry/flutter-aurora/bin/flutter test integration_test -d aurora-localhost:2223
```

Baseline (до начала работ): все 23 интеграционных теста зелёные.

## Чеклист

- [x] 1. **Утечка `errmsg` в `setKey`** (database.cpp: `sqlite3_free(errmsg)`
      отсутствует — утекает при каждом неверном ключе).
      **Находка неактуальна**: п. 6 (#27, слит) переписал `setKey()` на
      `sqlite3_key_v2` — пути `sqlite3_exec`/`errmsg` больше нет, терять нечего.
      PR #23 (`fix/free-errmsg-in-setkey`, e8f6acb) **закрыт как superseded**:
      P1 KotDath подтвердил ASan'ом, что его фикс (free до копирования через
      `const char*`) сам был heap-use-after-free, а после ребейза дифф откатывал
      бы фикс #27 обратно на `PRAGMA key`.
- [x] 2. **`pendingSqlCallbacks_`: завершать ошибкой при `close()`** — сейчас
      очередь сбрасывается молча, Dart future'ы висящие на операции не
      завершаются никогда.
      Ветка: `fix/fail-pending-on-close`, тест: `close_fails_pending_test.dart`.
      **В main** (`89dd0e6`, squash): drain в `close()` + null-гварды в 7 SQL-методах + **сброс
      состояния транзакции** после close (`transactionID_=0`,
      `currentTransactionID_=None`) + гвард `!db_` в `processSqlCommand()` до
      ветвления по transactionId. Без последних двух: команда из внешней
      `AsyncQueue`, дошедшая до `processSqlCommand()` ПОСЛЕ close, видела stale
      ID и уходила в `pendingSqlCallbacks_` закрытой БД → Dart-future висел
      вечно (KotDath подтвердил нативным harness: `MISSING RESPONSE,
      pending=1 transaction=1 isOpen=0`). Тест: сценарий KotDath — raw
      MethodChannel, worker-порядок busy-CTE → close → foreign SQL
      (`close_fails_pending_test.dart`, 3-й тест).
      **Это реальный фикс (не «просто на всякий»)**, в отличие от п. 5. Внешний
      аудит утверждал «pending — не утечка, `~MethodResult` освобождает Dart-объект»
      — это **ошибочно**, два независимых факта:
      1. `~MethodResult()` → `~ReplyManager()` (client_wrapper 1.14.0,
         `core_implementations.cc`) при уничтожении **без** ответа лишь печатает
         `Warning: Failed to respond to a message. This is a memory leak.` — ответ
         не шлётся, Dart-future не завершается, pending-структура движка течёт.
      2. Лочки в Dart **разные**: `close()` → `factory.closeDatabase` берёт
         per-path **open-лочку**, а операция берёт per-object **`_rawLock`** (в
         транзакции — вообще не берёт). Значит `close()` и in-flight операция НЕ
         взаимоисключающи: запрос в полёте (не awaited / в транзакции) + `close()`
         → без дренажа pending-колбэк бы пропал.
- [x] 3. **`deleteDatabase`: удалять `-wal`/`-shm`/`-journal`** и проверять
      результат `std::filesystem::remove` (в WAL-режиме зашифрованный `-wal`
      остаётся на диске после «удаления»).
      Ветка: `fix/delete-sidecar-files`, тест: `delete_sidecar_files_test.dart`.
      **В main** (`4868c2d`, squash; merge-base 077c605), 29 тестов зелёные. Review-раунд KotDath закрыт: non-throwing exists/remove с std::error_code, неудачи удаления → result->Error, репро из ревью (-wal = непустой каталог) добавлен в тест, ответ в треде.
- [x] 4. **Data race на `db_` и на состоянии транзакции**: `isOpen()`/`isInTransaction()`
      читают из канала-потока, пока worker меняет handle и `currentTransactionID_`
      (формально UB; TSan подтвердил обе гонки — KotDath, P2).
      Ветка: `fix/atomic-db-handle`, тест: `concurrent_open_close_test.dart`.
      **В main** (`2972617`, squash; ответ в треде 4371009420):
      `std::atomic<bool> open_` + `std::atomic<int> currentTransactionID_` (все 12 сайтов
      через .load()/.store(); счётчик `transactionID_` — worker-only, остался plain).
      Тест: (1) open/write/close-хаммеринг, (2) сценарий KotDath — прямой MethodChannel,
      1000× BEGIN/ROLLBACK ‖ 2000× re-open, dedup всегда один id. Нюанс протокола:
      ROLLBACK обязан нести `transactionId` из ответа на BEGIN, иначе `processSqlCommand`
      кладёт его в `pendingSqlCallbacks_` за активную транзакцию → вечно. 31/31 на эмуляторе.
- [ ] 5. **Гонка single-instance open** — **обойдено/отложено**: Dart-слой
      (`sqflite_common` 2.5.6: `_NamedLock` по пути + кэш helper) уже сериализует
      открытые по одному пути, поэтому с публичного API двойной нативный open
      невозможен — нативное дублирование даёт мало пользы. Тест на переиспользование
      упирается в **предсуществующий** (не наш) ханг первого `openDatabase` c
      `version`+`onCreate` в интеграционном контексте — отдельное расследование.
      **Не делаем нативно** (покрыто Dart-слоем).
- [x] **6. Затирание ключа через `sqlite3_key_v2`** — ~~`PRAGMA key` в SQL-строке ломалось на кавычках и обрезалось по NUL~~. **Готово** (ветка `fix/key-v2-wipe`, `45e1509`): `setKey()` применяет ключ через `sqlite3_key_v2(db, "main", buf, len)` на приватном `std::string`-буфере (без SQL-строки: кавычки/юникод как есть, NUL выживает — буфер ограничен длиной); буфер стирается (volatile-цикл) сразу после вызова. `SQLITE_HAS_CODEC` в CMake. Тест `key_special_chars_test`.
      Ветка: `fix/key-v2-wipe`, тест: `key_special_chars_test.dart`
      (кавычки, юникод, пустой ключ).
- [x] **7. Исключения в async queue** — **в main** (`956638a`, squash).
      Внешний аудит (2026-10-25, см. `sqflite_sqlcipher_aurora_memory_audit_2026-10-25.md`)
      повысил это до **critical**: пустой `catch (...) {}` в `AsyncQueue::executeTasks`
      глотал исключение задачи → `MethodResult` не завершался → Dart-future висел
      вечно, без следа. Подтверждённый репро: `openDatabase` под regular-file
      (`createParentDir` → `filesystem_error`). Фикс (полная перестройка, как
      предложил KotDath): `Reply` (one-shot обёртка над `MethodResult`,
      атомарный флаг завершения — двойной ответ исключён) + `enqueueTask`
      (try/catch вокруг задачи: убежавшее исключение завершает вызов ошибкой);
      все 9 обработчиков отвечают через `Reply`. `AsyncQueue::setExceptionHandler`
      (+ `Logger::error()`, логируется независимо от verbosity) — backstop.
      `createParentDir` — non-throwing `error_code` → `utils::error`. `deleteDatabase`
      — исправленная версия из #24 (в main).
      **Второй раунд KotDath (4372241703):** `enqueueTask` не покрывает
      **отложенные** SQL-callback'и — `processSqlCommand` сохраняет их в
      `pendingSqlCallbacks_` и сразу возвращается, а при последующем выполнении
      (drain из чужого COMMIT/ROLLBACK/close) их обёртка уже закончилась:
      исключение убивало drain и уходило в Reply чужого COMMIT (уже
      answered → игнор), а свой вызов оставался pending. Фикс: `guardSqlCallback`
      — тот же try/catch, привязанный к Reply **собственного** вызова, вокруг
      всех 6 processSqlCommand-колбэков. Тесты: `open_parent_file_test`
      (репро KotDath), +4-й тест в `delete_sidecar_files_test` (непустой каталог),
      `deferred_query_exception_test` (сентинел-SQL `THROW_STD_EXCEPTION` в
      отложенном query: свой вызов получает ошибку, drain продолжает, БД жива;
      сентинел — test-hook в query-обработчике, невалидный SQL). Полный сют 32/32.
- [~] **8. Мелочи** — проверено, в основном не требуется:
      - скобка: `formatError` уже даёт сбалансированные скобки (`error: message (desc)`) — фикс не нужен;
      - число аргументов: «мало» уже проверяется (`argIndex >= args.size()` → «Argument count mismatch», предохранитель от вылета за пределы вектора); «много» SQLite корректно игнорирует (совпадает с Dart) — менять не надо;
      - TTL заброшенных курсоров: не тестируется, **отложено**.

## Уже закрыто (аудит устарел по этим пунктам)

- ~~Пароль на нешифрованную БД молча игнорируется~~ — `open()`/`openReadOnly()`
  уже вызывают `validateOpenDatabase()` (пробный `PRAGMA user_version`), и
  `open_without_key_test.dart` покрывает все комбинации (неверный/пустой/
  отсутствующий ключ, plain-БД с паролем). Проверка `PRAGMA cipher` не нужна.
- ~~`close()` теряет handle при ошибке~~ — текущий `close()` при
  `sqlite3_close_v2 != OK` возвращает ошибку **до** `db_ = nullptr`, handle
  жив и DB остаётся в мапе. Осталось только залогировать (входит в п. 7).
- ~~Проверка ключа после `setKey`~~ — см. выше, `validateOpenDatabase()`.

## Аудит (оригинальные находки)

### Память
1. Утечка `errmsg` в `setKey` — `sqlite3_free(errmsg)` нигде нет. (Неактуально:
   путь удалён переписыванием на `sqlite3_key_v2`, п. 6/#27; PR #23 закрыт.)
2. `close()` при ошибке — см. «уже закрыто» (остался лог).
3. Заброшенные курсоры: запись в `cursors_` живёт до `close()`; нет лимита/TTL.
4. `pendingSqlCallbacks_` неограничен (дренаж в `close()` уже есть, см. п. 2).
5. `executeTasks` глотает все исключения (`catch (...) {}`).

### Безопасность
6. Пароль не затирается (живёт в `std::string` по всей цепочке).
7. Пароль на plain-БД — закрыто `validateOpenDatabase()` (см. выше).
8. Data race на `db_` (канал vs worker).
9. Гонка single-instance open.
10. `deleteDatabase` не удаляет `-wal`/`-shm`/`-journal`, результат не проверяется.

### Мелочи
- Скобка в `formatError(Errors::Closed, ...)`.
- Нет проверки числа аргументов в `query()`/`queryWithPageSize()`.
- `onDatabaseExistsCall`: блокирующий `std::filesystem::exists` в потоке канала.
- `AsyncQueue`: деструктор vs `push` из канала — проверить гарантию shutdown.

## Внешний аудит 2026-10-25 (`sqflite_sqlcipher_aurora_memory_audit_2026-10-25.md`)

Аудит сделан против публичного `main` (`b7fe5b3`) — **без** наших невлитых
веток. Перекрёстная сверка:

| Точка аудита | Статус |
|---|---|
| **3.** «Pending future — не утечка, `~MethodResult` освобождает Dart-объект» | **ОШИБКА аудита** — см. п. 2: `~ReplyManager` лишь печатает warning «memory leak», future не завершается; лочки `close()` и операции разные → наш `fix/fail-pending-on-close` **реален** |
| **5.** Исключения в async queue глотаются (critical) | **НОВОЕ** → `fix/handle-async-exceptions` (п. 7) |
| 6. WAL sidecar при `deleteDatabase` | уже в `fix/delete-sidecar-files` (п. 3) |
| 7. `errmsg` в `setKey` | п. 1 — неактуально после `sqlite3_key_v2` (#27 слит), PR #23 закрыт |
| 8. `sqflite_common` 2.5.6 (не 2.5.14) | мы и так на 2.5.6 — совпадает |

По-настоящему новой была только точка 5 (async-исключения). Точка 3 аудита
(«pending — не утечка») **ошибочна** — см. п. 2. Аудит также подтвердил нашу
threading-модель (open синхронно на канале, операции на воркере, мапы под mutex)
как корректную — новых гонок не нашёл. Остальные «high»-пункты аудита —
наши п. 1 и 3 (errmsg — неактуально после #27, WAL — закрыт #24, слит).

## Сравнение с соседней библиотекой (`../sqflite_sqlcipher`)

Сосед — классический Flutter-плагин: ObjC + FMDB (iOS/macOS), Java +
net.sqlcipher (Android). Архитектура другая (без C++/async-очереди), но
находки по нашим пунктам аудита:

### Что есть у соседа и стоит перенять

- **Проверка ключа после `setKey`** (iOS, `SqfliteSqlCipherPlugin.m`):
  ```objc
  [database setKey:password];
  FMResultSet *s = [database executeQuery:@"SELECT COUNT(*) FROM sqlite_schema"];
  if (s == nil) { passwordCheckSucceeded = false; ... }
  ```
  Неверный пароль → open падает с `open_failed`, и **не регистрируется**
  single-instance запись (иначе hot-restart вернул бы БД без ключа).
  У нас аналог — `validateOpenDatabase()` (`PRAGMA user_version`), уже есть.
- **`deleteDatabase` на Android** (`Database.java`) удаляет и побочные файлы:
  ```java
  file.delete();
  new File(file.getPath() + "-journal").delete();
  new File(file.getPath() + "-shm").delete();
  new File(file.getPath() + "-wal").delete();
  ```
  → наш пункт 3. (На их iOS — только основной файл, та же дыра.)
- **ReadOnly без удаления файла** (Android): кастомный
  `DatabaseErrorHandler.onCorruption` — пустой («default deletes the file,
  we don't want that»). Подтверждает наш подход «не удаляем БД при
  неудачном открытии».
- **Гонки**: ObjC-проперти объявлены `atomic`, мапы под `@synchronized`;
  Android сериализует сам open через `openCloseLocker` → у них race из
  наших пунктов 4/5 закрыт, нам нужен mutex/atomic.
- **Бонус: миграция** — Android при неудачном open делает retry с хуком
  `PRAGMA cipher_migrate` (мигрирует plain-БД в зашифрованную по паролю);
  если миграция падает — бросает исходное исключение. Можно рассмотреть.

### Чего у соседа НЕТ (мы делаем лучше)

- **Затирание ключа**: пароль живёт в `NSString`/`String` — в этих языках
  его в принципе нельзя стереть. Мы можем: `sqlite3_key_v2` + `explicit_bzero`
  (пункт 6).
- **Тихое игнорирование пароля на plain-БД**: их iOS-чек
  (`SELECT COUNT(*) FROM sqlite_schema`) на plain-файле **успешно проходит**
  — проблема у них осталась. На Android net.sqlcipher громко падает
  («file is not a database»). У нас закрыто `validateOpenDatabase()`.
- **Утечка `errmsg`, pending-очереди, зависшие future** (наши пункты 1–2, 5):
  аналогов нет — они полностью материализуют результаты (курсоры/пагинация
  отсутствуют), а ключ обрабатывает FMDB/библиотека.
