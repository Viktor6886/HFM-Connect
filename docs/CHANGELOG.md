# История изменений

Формат: [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/), версии прошивки — `HFM_FW_VERSION`.

## [0.1.0] — 2026-11-08

### Добавлено
- Платформонезависимое ядро: `HfmSession` (транзакции префикс/селектор/аргумент, эхо/ACK,
  дедлайн 750 мс, 3 попытки синхронизации, белый список префиксов, токен записи),
  `TraceLog` (RAW TX/RX, JSON/JSONL), `HfmDecode` (формулы HFMreader, LUT HHT),
  `HfmIdent` (F0 28 байт), `HfmDtc` (152 позиции, последовательные ремапы, каталог 163 кодов),
  `HfmProfiles` (16 записей автомобилей, 6 профилей разъёмов, профили ЭБУ, capability, нормы I07),
  `HfmLive` (full8, расширенные рецепты, 3 слоя оценки), `HfmServices` (адаптации, кодировка,
  VIN, карантинные рецепты стирания/сброса/продувки), `ServiceGuard`, `SimEcu`, `ImpulseDecoder`.
- Arduino-слой: `KLineUart` (ESP-IDF UART, BREAK через инверсию, эмуляция SPACE), `DiagEngine`
  (задача FreeRTOS, задания, опрос, keep-alive, watchdog теста, запись CSV), `WebApi` (REST),
  `NetManager` (AP+STA, captive DNS, mDNS), `Settings` (NVS), `HFMConnect.ino`.
- WebUI (iOS-стиль, 11 разделов, светлая/тёмная тема, графики canvas) встроен gzip.
- Host-тесты (39), трасса replay, сверка таблиц, mock-сервер WebUI, GitHub Actions CI.
- Документация: README, CLAUDE.md, ARCHITECTURE, PROTOCOL, ECU_COMPATIBILITY,
  DIAGNOSTIC_FUNCTIONS, HARDWARE, BUILD, TESTING, DECISIONS, ROADMAP, PROGRESS,
  SESSION_HANDOFF, SOURCES; спецификации REV 1.1/1.3/1.4 в `docs/specs/`.

### Известные ограничения
- Ничего не проверено на реальном ЭБУ; тайминги и уровни трансивера не измерены.
