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
| USB CDC On Boot | **Disabled** (обязательно: разъём USB-OTG занят FTDI-адаптером; лог — через разъём UART/COM) |
| USB Mode | **Hardware CDC and JTAG** (не «USB-OTG (TinyUSB)») |
| Flash Size | 4MB (или фактический размер) |
| Partition Scheme | Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS) — прошивка с USB-хостом занимает ~1,18 МБ (90 %). При росте прошивки: *Huge APP (3MB No OTA/1MB SPIFFS)* |
| PSRAM | любой (не требуется) |
| Upload Speed | 921600 |

6. На ESP32-S3 Zero отсоедините KKL и внешнее питание, подключите плату к ПК через её USB-C. На плате с отдельным UART/COM используйте этот разъём. Выберите порт и нажмите **Загрузить**.
   При неверных настройках USB компиляция остановится с `#error` и подсказкой.
   Если загрузка не начинается — удерживайте BOOT, нажмите RESET, отпустите BOOT.
7. Монитор порта 115200: прошивка выведет имя точки доступа.

Раздел LittleFS форматируется автоматически при первом запуске (используется для записей
Live Data и отчётов). Веб-интерфейс встроен в прошивку (`WebUiAssets.h`) — отдельная
загрузка файловой системы не нужна.

## Первый запуск

1. Подключитесь к Wi-Fi **HFM Connect**, пароль по умолчанию `12345678`
   (публичный, задокументирован; меняется в *Настройки → Wi-Fi*). Устройства со старым
   паролем по умолчанию `hfm12345` переходят на новый при загрузке; пароль, заданный
   пользователем, сохраняется. Надпись Android «Войти: …» — системная, это не часть WebUI.
2. Откройте `http://192.168.4.1/` (или `http://hfm.local/`). На большинстве телефонов
   откроется автоматически (captive portal).
3. FTDI-адаптер — в разъём USB-OTG, кабель — к X11/4, зажигание ВКЛ. Подключение к ЭБУ
   выполняется **автоматически** (после загрузки и при появлении адаптера); ход видно на
   странице *Подключение*. Автоподключение отключается в *Настройках*. Тайминги линии
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

## OLED-дисплей: библиотека и схема разделов

Прошивка с дисплеем (`HFM_OLED_ENABLE 1` в `Config.h`, по умолчанию) требует:

1. Библиотеку **U8g2** (Arduino IDE: *Инструменты → Управление библиотеками → U8g2*).
2. Схему разделов ***Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)***: с
   дисплеем прошивка занимает 1 342 320 Б и в раздел 1,25 МБ схемы по умолчанию не
   помещается (в Huge APP — 42 %). Настройки (NVS) при смене схемы сохраняются; LittleFS
   форматируется заново — записи и отчёты с устройства скачайте заранее.

Без дисплея: `#define HFM_OLED_ENABLE 0` в `Config.h` — библиотека U8g2 не нужна, схема
разделов по умолчанию подходит.

Просмотр экранов на ПК без дисплея: `python tools/oled_preview.py` →
`build/oled_preview/screens.png` (нужны U8g2, Pillow и компилятор C/C++ или `ziglang`).
Изображения: `python tools/gen_oled_bitmaps.py` → `OledBitmaps.h`.

## arduino-cli (CI и командная строка)

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install U8g2
# с дисплеем (по умолчанию)
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=default firmware/HFMConnect
# без дисплея
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=default,CDCOnBoot=default \
  --build-property "compiler.cpp.extra_flags=-DHFM_OLED_ENABLE=0" firmware/HFMConnect
arduino-cli upload  --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app -p COM5 firmware/HFMConnect
```

## Host-тесты (без ESP32)

```bash
python tools/run_host_tests.py
```

Компилятор: `g++`/`clang++`, либо `pip install ziglang` (Windows без MinGW).
Генераторы: `python tools/gen_dtc_catalog.py` (DtcCatalog.h из CSV),
`python tools/check_tables.py` (сверка таблиц прошивки с данными анализа).

## Сохранение настроек при обновлении прошивки

Все настройки (тайминги, Wi-Fi, автомобиль и дата выпуска, тема, параметры графика Live
Data, 9-значный код, рабочий профиль пробуждения, сведения о ЭБУ) хранятся в NVS ESP32-S3 и
переживают перепрошивку. Не включайте в Arduino IDE «Erase All Flash Before Sketch Upload»:
этот пункт стирает NVS.

## Запас памяти программы

С 0.8.0 сборка с OLED-дисплеем идёт со схемой Huge APP (42 % из 3 МБ). Без дисплея
прошивка занимает 98 % раздела схемы «Default 4MB with spiffs». При переходе на
*Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)*:
раздел NVS остаётся на месте (настройки сохраняются), LittleFS форматируется заново
(записи и отчёты на устройстве будут удалены — скачайте их заранее).
