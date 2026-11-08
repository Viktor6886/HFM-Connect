# CLAUDE.md — постоянные инструкции для Claude Code

Проект: **HFM Connect** — диагностика Mercedes-Benz Bosch HFM-SFI (M104/M111) по K-Line на
ESP32-S3 с внешним трансивером и автономным WebUI. Язык общения и документации — русский;
идентификаторы кода — английские.

## Перед началом работы (обязательно)

1. Прочитать `docs/SESSION_HANDOFF.md`, `docs/PROGRESS.md`, `docs/ROADMAP.md`, этот файл.
2. `git log --oneline -20` и `git status` — фактическое состояние.
3. Запустить тесты: `python tools/run_host_tests.py` и `python tools/check_tables.py`.
4. Не начинать заново выполненные задачи и не переписывать работающие модули без причины.

## Жёсткие правила предметной области

- **Не выдумывать** HEX-команды, распиновки, формулы и тайминги. Каждый факт — с источником
  и уровнем подтверждения (ЗАВ / РЕК / ГИП / VAL / БЛОК, см. `docs/PROTOCOL.md` §0).
- Не реализовывать OBD-II, ELM327, CAN, UDS. DM N59/1 — отдельный блок, вне области.
- Неподтверждённые функции не выдавать за реализованные: в UI — «заблокировано» с причиной.
- Операции записи (стирание DTC, сброс адаптаций, исполнительные тесты, кодирование):
  только через `ServiceGuard`; проверка в прошивке, не в UI; без автоповтора; FAULT_STOP.
- Новые префиксы — только через `classifyPrefix()` с источником; перебор кодов запрещён.
- Год/модель автомобиля не разблокирует команды — только идентификация ЭБУ.
- Банки F1 (C-01) не называть «текущие/сохранённые» без подтверждения профиля.
- Заводские PDF Mercedes-Benz не коммитить (авторские права, D-015).
- Не публиковать секреты. Пароль AP по умолчанию `hfm12345` — публичный, документирован.

## Архитектурные правила

- Платформонезависимое ядро (`Hfm*.cpp`, `TraceLog`, `ServiceGuard`, `ImpulseDecoder`)
  **не включает Arduino.h**; Arduino-код — в `#ifdef ARDUINO`.
- Только `DiagEngine` владеет K-Line; HTTP — через `submit(Job)` и снимки под `lock()`.
- Внешние библиотеки Arduino не добавлять (D-002). JSON — `JsonWriter`.
- Главный файл прошивки: `firmware/HFMConnect/HFMConnect.ino`; модули — рядом (Arduino IDE).
- После правки `webui/*` — `python tools/build_webui.py` (CI проверяет актуальность).
- После правки `data/protocol/dtc.csv` — `python tools/gen_dtc_catalog.py`.
- Режима симулятора в прошивке нет (D-016): `SimEcu` живёт только в `tests/host/`.
- В текстах репозитория не упоминать сторонние программы как основу ПО (D-017);
  ссылаться на спецификацию REV 1.1–1.4 и заводские документы.
- Новые решения — в `docs/DECISIONS.md` (D-0xx).

## Статусы функций (`docs/DIAGNOSTIC_FUNCTIONS.md`)

запланирована → разрабатывается → реализована → протестирована программно →
проверена на реальном ЭБУ. **Не ставить «проверена на ЭБУ» без трассы с реального блока.**
SimEcu и replay-трассы — это программные испытания.

## Проверки перед коммитом

```bash
python tools/run_host_tests.py
python tools/check_tables.py
python tools/build_webui.py --check
node --check webui/app.js
arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/HFMConnect   # если доступен
```

## Завершение сессии (обязательно)

1. Синхронизировать документацию с кодом: PROGRESS, DIAGNOSTIC_FUNCTIONS, CHANGELOG,
   DECISIONS, TESTING (результаты), ROADMAP.
2. Обновить `docs/SESSION_HANDOFF.md`: что сделано, какие файлы изменены, что проверено,
   какие ошибки остались, следующие шаги.
3. Прогнать тесты, commit, push. Коммиты заканчиваются строкой
   `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
4. Нерешённое — GitHub Issues (`gh issue create`), метки `hardware-verification`, `protocol`,
   `enhancement`, `bug`.

## Локальное окружение владельца

- Рабочая копия: `C:\Users\pvpwm\Desktop\HFM-Connect\repo` (клон GitHub).
- Исходная документация (не в git): `C:\Users\pvpwm\Desktop\HFM-Connect\Documents`,
  `…\Protocol view` (материалы анализа), спецификации REV 1.1–1.4 в корне.
- arduino-cli: `C:\Users\pvpwm\tools\arduino-cli\arduino-cli.exe`, ядро esp32 3.3.8.
- C++ на Windows: `pip install ziglang` (используется `run_host_tests.py` автоматически).
- Диск C: почти заполнен — не скачивать дополнительные ядра/тулчейны без необходимости.
