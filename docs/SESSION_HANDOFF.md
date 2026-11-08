# Передача состояния следующей сессии

**Дата:** 2026-11-08 · **Сессия:** 1 (создание проекта с нуля) · **Ветка:** `main`
**Последний зелёный CI:** run 42505821360 — host-тесты и компиляция ESP32-S3 успешны,
0 предупреждений компилятора.

## 1. Что сделано

- Изучены: REV 1.1 (357 с.), REV 1.3/1.4, `HFM_PROTOCOL_FULL_GPT.md`, отчёты HFMreader
  (protocol.md, dtc, live_data, hht_recipes, байткод `apply_variant_remaps`), HFMScan
  (keep-alive 10 00, F9, F1 30–32/40–42, обратная трактовка банков), HHTwin, заводские
  PDF I03–I10, E111, WIS, Test & Adjustment Data, инструкции импульсных считывателей.
- Создан репозиторий с нуля (был только README): прошивка, WebUI, тесты, CI, документация.
- Прошивка `firmware/HFMConnect/HFMConnect.ino` + 19 модулей; внешних библиотек нет.
- 14 GitHub Issues (#1–#14), метки hardware-verification / protocol / enhancement / blocked-by-source.

## 2. Ключевые файлы

| Файл | Роль |
|---|---|
| `firmware/HFMConnect/HfmSession.*` | транзакции, политика команд (`classifyPrefix`) |
| `firmware/HFMConnect/HfmProfiles.*` | автомобили, разъёмы, профили ЭБУ, capability, нормы |
| `firmware/HFMConnect/HfmLive.*`, `HfmDecode.*` | Live Data, формулы, LUT |
| `firmware/HFMConnect/HfmDtc.*`, `DtcCatalog.h` (генерир.) | DTC |
| `firmware/HFMConnect/HfmServices.*`, `ServiceGuard.*` | чтение/запись, защита |
| `firmware/HFMConnect/KLineUart.*` | UART ESP32-S3 (Arduino) |
| `firmware/HFMConnect/DiagEngine.*` | задача-владелец K-Line, задания |
| `firmware/HFMConnect/WebApi.*`, `NetManager.*`, `Settings.*` | HTTP API, Wi-Fi, NVS |
| `firmware/HFMConnect/WebUiAssets.h` (генерир.) | WebUI gzip |
| `webui/index.html, app.css, app.js` | исходники WebUI |
| `tests/host/*.cpp`, `tests/traces/sim_*.jsonl` | host-тесты, синтетическая трасса |
| `tools/*.py` | run_host_tests, check_tables, build_webui, gen_dtc_catalog, dev_server |

## 3. Что проверено

| Проверка | Результат |
|---|---|
| Host-тесты (Windows zig c++ и Linux g++ в CI) | 39 тестов, 471 проверка, 0 ошибок |
| check_tables.py | LUT/маски/базовые коды/ремапы/каталог = данные анализа |
| Компиляция ESP32-S3 (CI, esp32 core latest) | успешно, 0 предупреждений: 1 114 301 Б (85 % APP 1,25 МБ), RAM 78 КБ |
| Компиляция локально (esp32 3.3.8, Windows) | первая — ошибка `size_t` (исправлена); повторная — см. `build/compile_local.log` |
| WebUI на mock-сервере в браузере | все разделы открываются; исправлены гонки рендера при смене раздела |
| **На устройстве ESP32-S3** | **не запускалось** (нет платы в сессии) |
| **На реальном ЭБУ** | **не проводилось** |

## 4. Известные проблемы и риски

1. Схема трансивера (docs/HARDWARE.md) — проектная: уровни L9637D 5 В ↔ 3,3 В требуют
   проверки по даташиту; не изготовлена.
2. Эмуляция SPACE (EVEN/ODD) и BREAK через инверсию TXD не проверены осциллографом (D-003).
3. Приём ответов ЭБУ в режиме EVEN — гипотеза; если ЭБУ отвечает иначе, возможны
   ошибки кадра — они считаются (`parity_errors/frame_errors` в /api/status).
4. Синхронный WebServer: долгие chunked-ответы (`/api/report`, `/api/trace.jsonl`)
   блокируют другие запросы на время передачи.
5. (исправлено в конце сессии) id вытесняющего задания STOP теперь отражается в /api/status.
6. Прошивка занимает 85 % раздела APP по умолчанию — при росте переключиться на Huge APP.
7. Записи базы автомобилей с меткой ГИП (W124 E220 111.960, W202 C230 111.974) требуют
   сверки с документацией.

## 5. Следующие шаги (по приоритету)

1. Проверить CI последнего коммита (`gh run list`), локальный лог `build/compile_local.log`.
2. Залить прошивку на ESP32-S3, проверить WebUI в режиме «Симулятор» на телефоне (iOS/Android),
   память (`heap` в /api/status), captive portal.
3. Issue #1: собрать адаптер, осциллограммы BREAK/AA/паритета.
4. Issue #2: первая реальная трасса F0 → `tests/traces/ecu_*.jsonl` + replay-тест.
5. Issue #3 (C-01), #4 (сверка Live Data), #5 (сбор ID ЭБУ) → профили уровня VAL.
6. Только после трасс: расширение capability (ROADMAP этап C).

## 6. Как продолжить (команды)

```bash
cd C:/Users/pvpwm/Desktop/HFM-Connect/repo
git pull && git log --oneline -10
python tools/run_host_tests.py && python tools/check_tables.py
python tools/dev_server.py 8080          # WebUI без устройства (mock)
C:/Users/pvpwm/tools/arduino-cli/arduino-cli.exe compile --fqbn esp32:esp32:esp32s3:PartitionScheme=default,CDCOnBoot=cdc --build-path build/fw firmware/HFMConnect
```

Диск C: почти заполнен (~2–4 ГБ свободно) — не устанавливать дополнительные ядра.
