# Архитектура HFM Connect

## Обзор

```
 Смартфон / ПК (браузер)                 ESP32-S3
 ┌────────────────────┐   Wi-Fi AP/STA  ┌──────────────────────────────────────────────────┐
 │ WebUI (SPA, iOS)   │◄──HTTP/JSON────►│ WebApi (WebServer, core 1, loop())               │
 │ webui/*.{html,js}  │                 │   │ submit(Job)            ▲ lock()/state()      │
 └────────────────────┘                 │   ▼                        │                     │
                                        │ DiagEngine (FreeRTOS task, core 0) — владеет K-Line│
                                        │   ├─ HfmSession ── политика команд, эхо/ACK      │
                                        │   │     └─ ByteLink ◄─ KLineUart (UART1)         │
                                        │   ├─ HfmIdent / HfmProfiles (профиль, capability)│
                                        │   ├─ HfmDtc (+ DtcCatalog)                       │
                                        │   ├─ HfmLive (+ HfmDecode, нормы)                │
                                        │   ├─ HfmServices (чтение / карантинные рецепты)  │
                                        │   ├─ ServiceGuard (prepare/authorize/FAULT_STOP) │
                                        │   └─ TraceLog (RAW TX/RX, JSONL)                 │
                                        │ NetManager (AP+STA, DNS, mDNS) · Settings (NVS)  │
                                        │ LittleFS: /rec/*.csv, /reports/*.json            │
                                        └───────┬──────────────────────────────────────────┘
                                                │ USB-OTG (FtdiLink, USB-хост) или UART1 (KLineUart)
                                     FTDI K-Line адаптер / трансивер ── X11/4 ── ЭБУ N3/4 HFM-SFI
```

## Слои и правила зависимостей

| Слой | Файлы | Зависит от | Платформа |
|---|---|---|---|
| Типы | HfmTypes.h, JsonWriter.h | — | любая |
| Транспорт (интерфейс) | ByteLink.h | Типы | любая |
| Транзакции | HfmSession, TraceLog | ByteLink | любая |
| Интерпретация | HfmDecode, HfmIdent, HfmDtc, DtcCatalog, HfmProfiles, HfmLive | Типы | любая |
| Сервисы | HfmServices, ServiceGuard, ImpulseDecoder | Транзакции, Интерпретация | любая |
| Тестовые модели | SimEcu, ReplayLink (только `tests/host/`) | ByteLink | ПК |
| Аппаратура | KLineUart | ByteLink, ESP-IDF uart | Arduino |
| Приложение | DiagEngine, WebApi, NetManager, Settings, HFMConnect.ino | всё | Arduino |

Платформонезависимые модули не включают `Arduino.h` и компилируются host-тестами
(`tools/run_host_tests.py`). Arduino-модули обёрнуты в `#ifdef ARDUINO`.

## Потоки и синхронизация

- **DiagEngine** — единственный владелец K-Line (REV 1.4 §19: «один собственник порта»).
  HTTP-обработчики не обращаются к линии: они ставят задание (`submit`) и читают снимок
  состояния под рекурсивным мьютексом.
- Одно задание в очереди; `STOP` и `live_stop` вытесняют. Состояние задания (`queued →
  running → done/failed` + сообщение) отдаётся в `/api/status`, WebUI опрашивает его.
- TraceLog имеет собственный мьютекс (запись из задачи движка, чтение из HTTP).
- Цикл движка: аппаратные входы → задание → опрос Live Data (≥ 100 мс) или keep-alive →
  watchdog исполнительного теста → сброс буфера записи на LittleFS (2 с).

## Модель безопасности (fail-closed)

1. `classifyPrefix()` — белый список; неизвестные коды никогда не передаются.
2. Префиксы записи требуют токена `armWrite()`; токен снимается после каждой операции.
3. `prof::resolve()` выдаёт capability только по ID ЭБУ; запись — только эталонный ЭБУ
   (РЕК) + «Режим инженера».
4. `ServiceGuard`: prepare (проверки, токен 20 с, фраза) → authorize (повторная проверка,
   ключ WRITE-EN, тот же отпечаток ЭБУ) → выполнение без повторов → при ошибке `FAULT_STOP`.
5. Исполнительный тест: лимит ВКЛ 30 с и общий лимит 180 с — таймеры прошивки, не WebUI.
6. Неизвестные величины (напряжение без АЦП, устаревшие обороты) = «неизвестно», не «ok».

## REST API (кратко)

| Метод | Путь | Назначение |
|---|---|---|
| GET | /api/status | состояние сеанса, задания, сети, ключа, автомобиля |
| GET | /api/ecu | идентификация, профиль, capability, адаптации, кодировка, VIN |
| GET | /api/dtc | банки, декодированные коды |
| GET | /api/live[?defs=1] | значения, качество, нормы, RAW групп |
| GET | /api/services | операции, план, результат |
| GET | /api/vehicles | автомобили, разъёмы, нормы |
| GET | /api/trace?after=N&max=M | события TX/RX |
| GET | /api/trace.jsonl | экспорт трассы |
| GET | /api/report[?dl=1] | полный отчёт JSON |
| POST | /api/job (job=…, op=…, token=…, phrase=…) | задания движка |
| GET/POST | /api/settings | настройки |
| POST | /api/record (on=1/0) | запись Live Data в CSV |
| GET | /api/files, /api/file?path= ; POST /api/file/delete | файлы LittleFS |
| POST | /api/report/save?name= | сохранить отчёт (тело — JSON) |
| POST | /api/reboot | перезагрузка |

Задания: `connect, disconnect, identify, read_dtc, read_adapt, read_coding, read_vin,
live_start, live_stop, svc_prepare, svc_execute, purge_on, purge_off, stop, impulse_read, ack_fault`.

## Память

TraceLog 1536 × 16 Б ≈ 24 КБ; WebUI ≈ 20 КБ gzip во flash; ответы JSON ≤ 8 КБ или chunked.
