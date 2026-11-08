# Сборка и загрузка прошивки

## Arduino IDE (основной способ)

1. Установите **Arduino IDE 2.x**.
2. *Файл → Настройки → Дополнительные ссылки менеджера плат*:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. *Инструменты → Плата → Менеджер плат* → установите **esp32 by Espressif Systems**
   версии **3.x** (проверено на 3.3.8).
4. Откройте `firmware/HFMConnect/HFMConnect.ino`. Все модули `.h/.cpp` лежат в той же
   папке и откроются как вкладки. **Внешние библиотеки не нужны** (используются только
   WebServer, WiFi, DNSServer, ESPmDNS, LittleFS, Preferences из состава ядра).
5. Настройки платы (*Инструменты*):

| Параметр | Значение |
|---|---|
| Плата | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled (лог в Serial Monitor по USB) |
| Flash Size | 4MB (или фактический размер) |
| Partition Scheme | Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS) — прошивка занимает ~1,11 МБ (85 %). При росте прошивки: *Huge APP (3MB No OTA/1MB SPIFFS)* |
| PSRAM | любой (не требуется) |
| Upload Speed | 921600 |

6. Подключите плату USB-кабелем, выберите порт, нажмите **Загрузить**.
   Если загрузка не начинается — удерживайте BOOT, нажмите RESET, отпустите BOOT.
7. Монитор порта 115200: прошивка выведет имя точки доступа.

Раздел LittleFS форматируется автоматически при первом запуске (используется для записей
Live Data и отчётов). Веб-интерфейс встроен в прошивку (`WebUiAssets.h`) — отдельная
загрузка файловой системы не нужна.

## Первый запуск

1. Подключитесь к Wi-Fi **HFM-Connect-XXXX**, пароль по умолчанию `hfm12345`
   (публичный, задокументирован; смените в *Настройки → Wi-Fi*).
2. Откройте `http://192.168.4.1/` (или `http://hfm.local/`). На большинстве телефонов
   откроется автоматически (captive portal).
3. Подключите адаптер к X11/4, зажигание ВКЛ, *Подключение → Подключить*. Тайминги линии
   (BREAK / пауза / тайм-аут) меняются в *Настройках* без перепрошивки.

## Изменение пинов

`firmware/HFMConnect/Config.h`: `HFM_KLINE_TX_PIN`, `HFM_KLINE_RX_PIN`, `HFM_WRITE_EN_PIN`,
`HFM_VBAT_ADC_PIN` (−1 = не установлен), инверсия сигналов трансивера.

## Изменение WebUI

Исходники — `webui/index.html`, `app.css`, `app.js`. После правки:

```bash
python tools/build_webui.py
```

Скрипт склеивает файлы, сжимает gzip и пишет `firmware/HFMConnect/WebUiAssets.h`.

## arduino-cli (CI и командная строка)

```bash
arduino-cli core install esp32:esp32
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=default,CDCOnBoot=cdc firmware/HFMConnect
arduino-cli upload  --fqbn esp32:esp32:esp32s3 -p COM5 firmware/HFMConnect
```

## Host-тесты (без ESP32)

```bash
python tools/run_host_tests.py
```

Компилятор: `g++`/`clang++`, либо `pip install ziglang` (Windows без MinGW).
Генераторы: `python tools/gen_dtc_catalog.py` (DtcCatalog.h из CSV),
`python tools/check_tables.py` (сверка таблиц прошивки с данными анализа).
