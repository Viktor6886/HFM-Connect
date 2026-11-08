# HFM_PROTOCOL_FULL — исследовательская спецификация Bosch HFM-SFI для Mercedes M104/M111
**Редакция:** 23.10.2026 · **Основа:** 7 предоставленных архивов отчётов REA/Ghidra (HFMreader, HFMScan, HHT-WIN).
**Статус:** реконструкция по поставленным пользователем двоичным программам, восстановленным структурам и отчётам; не заводская официальная спецификация. Прямая проверка на автомобиле не проводилась в этом исследовании.
**Цель:** инженерно пригодная, но консервативная карта обмена, ошибок, данных и сервисных функций для прототипа ESP32-S3/FT232RL. Включение операций изменения состояния ЭБУ требует отдельной валидации и блокировок.

## Оглавление

1. Область применимости, доказательства и уровни уверенности
2. Инвентаризация семи отчётных архивов
3. Аппаратное соединение и USB FT232RL
4. Физическая передача, UART и синхронизация
5. Модель транзакции HFM и автоматы состояний
6. Полная объединённая номенклатура команд
7. Параметры реального времени HFMreader
8. Формулы и точные температурные таблицы
9. Расширенные команды Live Data и дескрипторы HFMScan
10. Заводские структуры HHT-WIN: профили, тайминги, телеграммы
11. Заводские диагностические экраны HHT-WIN
12. Ошибки: банки, биты, каталоги, кодировки
13. Все битовые карты HHT-WIN
14. Полная текстовая карта DTC HHT-WIN
15. Вариантозависимые исправления DTC и сравнение HFMScan
16. Идентификация и совместимость М104/М111
17. Стирание DTC, адаптации и тесты исполнительных устройств
18. Обработка исключений, ограничения, защита двигателя
19. Архитектура ESP32-S3 и приёмочные испытания
20. Противоречия и невосстановленные функции
21. Контроль доказательств и реестр источников

## 1. Область применимости, доказательства и уровни уверенности

Документ ограничен **HFM-SFI на M104/M111**; сведения о PMS, LH-SFI, EZ, E-GAS, АКПП и отдельных блоках приведены только там, где они помогают не перепутать протоколы. Модель кузова сама по себе не определяет ЭБУ. Запросы применяются только после чтения идентификатора конкретного блока.
**Статусы:** `КОД` — статически читается из машинного или Python-байт-кода; `ДАННЫЕ` — присутствует в поставленном JSON/INI/структуре; `ПРОИЗВОДИТЕЛЬ_ПО` — заявленная автором диагностики полевая проверка, не проведённая здесь; `ГИПОТЕЗА` — смысл байтов/формулы восстановлен предположительно; `НЕ ПРОВЕРЕНО НА ЭБУ` — нет записанного обмена с физическим блоком. Даже прямое совпадение в двух программах ещё не означает работу на всех ревизиях.
**Три слоя байтов:** (а) реальные байты UART HFM; (б) USB-пакеты FTDI, где первые 2 байта каждого Bulk-IN пакета являются статусом FTDI; (в) внутренние сериализованные шаблоны HHT-WIN `CMD 01 ARG 00`. Последние НЕ являются прямой последовательностью UART, пока не разобран исполнитель шаблона.
Имеющиеся офлайн-тесты доказывают корректность функций на подготовленных входных данных, но не подтверждают физическую связь, стабильность таймингов или реализацию операций записи конкретным ЭБУ.
## 2. Инвентаризация семи отчётных архивов

| Архив | Файлов | Распаковано, MiB | MD | Примечание |
| --- | --- | --- | --- | --- |
| HFMreader_REPORT_0(1).zip | 393 | 11 | 10 | FINAL_REPORT_RU.md; README.md |
| HFMreader_REPORT_1.zip | 1018 | 41 | 13 | см. ниже |
| HFMreader_REPORT_2.zip | 1006 | 41 | 9 | см. ниже |
| HFMreader_REPORT_3.zip | 521 | 176 | 17 | HFM_M104_M111.md |
| HFMreader_REPORT_4.zip | 1300 | 441 | 40 | см. ниже |
| HFMScan_REPORT.zip | 152 | 59 | 1 | см. ниже |
| HHTwin_REPORT.zip | 3561 | 247 | 0 | см. ниже |

Всего физических файлов в архивах: **7951**. Эти числа включают повторные копии дистрибутивов, библиотек CPython и много дублей тех же самых отчётов, поэтому не являются числом независимых исследовательских результатов.
Для каждого архива выполнено чтение каталога ZIP и извлечение файлов. Проверено наличие ключевых источников и корректность JSON/CSV в используемой выборке. Не осуществлялся новый запуск Ghidra на каждом бинарном модуле: там, где приводятся адреса, это проверка существующего экспорта REA/Ghidra, а не независимая реконструкция с нуля.
### 2.1 Где находятся первичные данные

| Краткое имя | Путь внутри архива | Что содержит |
| --- | --- | --- |
| R0 | HFMreader_REPORT_0/HFMreader_REPORT_0/protocol/protocol.md | Python-байт-код HFMreader, ссылки на строки и смещения; вариант с восстановленным EXE |
| R0-T | HFMreader_REPORT_0/HFMreader_REPORT_0/exports/commands.json | Команды и байтовые роли, профили и источники |
| R0-L | HFMreader_REPORT_0/HFMreader_REPORT_0/exports/live_data.csv | Расшифрованные данные/кандидаты |
| R0-D | HFMreader_REPORT_0/HFMreader_REPORT_0/dtc/dtc.csv; dtc/bit_map.csv | Каталог и таблица битов |
| R1 | HFMreader_REPORT_1/HFMreader_REPORT_1/extracted/release_zip/.../_internal/hfmreader/hht_experimental/candidates.json | Заводские/экспериментальные данные, рецепты и LUT |
| S | HFMScan_REPORT/HFMScan_REPORT/protocol/commands.json | Транзакции и сервисные команды Delphi |
| S-D | HFMScan_REPORT/HFMScan_REPORT/reports/DTC_REPORT_RU.md; dtc/dtc_bitmaps.json | Ошибки, битовые карты |
| H-SG | HHTwin_REPORT/HHTwin_REPORT/protocol/sg_table_c814.json | Сценарии, шаблоны и тайминги HHT-WIN |
| H-L | HHTwin_REPORT/HHTwin_REPORT/work/istwert_records.json; work/hfm_tables.json | Заводские экраны, таблицы, шаблоны, конверсии |
| H-D | HHTwin_REPORT/HHTwin_REPORT/dtc/dtc_bit_tables.json; dtc/fault_text_records.json | Заводские битовые карты и сообщения неисправностей |

## 3. Аппаратное соединение и USB FT232RL

Рассматривается USB VAG KKL на FT232RL с физической 12-вольтовой K-Line. Подлинность FTDI и распиновку китайского адаптера проверить отдельно. USB-S3 необходимо перевести в режим Host с правильным источником VBUS 5 В и защитой питания; ESP32 GPIO запрещено соединять с K-Line напрямую.
| Контакт/сигнал | W124 16-контактный X11/4 | W140 38-контактный X11/4 | OBD-сторона KKL | Достоверность |
| --- | --- | --- | --- | --- |
| GND | 1 | 1 | 4/5 | типовая HFM-схема, сверить по VIN |
| VBAT | 16 | 3 | 16 | типовая HFM-схема, сверить по VIN |
| K-Line HFM | 8 | 4 | 7 | типовая HFM-схема, сверить по VIN |

FT232RL должен быть в USB Host среде представлен как USB FTDI (`0403:6001` у типового устройства), но клоны могут иметь другой идентификатор. FTDI VCP и D2XX — разные варианты программного доступа к той же микросхеме; HFMreader использует Win32 COM-путь, HFMScan содержит обращения к D2XX.
Питание через диагностический разъём, предохранитель, защиту перенапряжений, проверку положения переключателя KKL, отсутствие КЗ и изоляцию платы обеспечить до первого подключения к автомобилю.
## 4. Физическая передача, UART и синхронизация

| Поле | HFMreader по восстановленному байт-коду | HFMScan по Ghidra | Инженерная трактовка |
| --- | --- | --- | --- |
| Скорость | 9600 бод | 9600 бод | Совпадает |
| Формат CMD | 8N1 (\`none\`) | 8N1 | 9-й информационный признак = 1: гипотеза на базе поведения UART |
| Формат PARAM | 8S1 (\`space\`) | SPACE | бит чётности = 0 |
| BREAK | 177 мс | BREAK+программные задержки | значение 177 мс — константа HFMreader; проверить линию |
| Пауза после BREAK | 214 мс | вариантная задержка | значение 214 мс — константа HFMreader |
| Синхронизация | TX AA, RX AA 55 | AA и ожидание 55 | AA в RX первое — локальное эхо |
| Ожидание 2 байт | 0,75 с | настраивается | различать тайм-аут операции и Windows COM |
| Win32 interval/total | 50/100 мс | FTDI D2XX и INI | не путать с периодом опроса |
| Повторы | до 3, пауза 1 с (reader); управление с особой политикой | до повторов в init | контролировать лимит и запрет повторов записи |

Детали: HFMreader `serial_transport.HFMSerialTransport.initialize` L35–51; `winserial.configure_dcb` L46–50; `winserial.read_exact` L139–152. HFMScan: `0x4935A4`, `0x494450`, `0x494798`, `0x4952F0` из экспортов Ghidra. На USB FTDI BREAK/чётность реализуются vendor-control transfers, а не пересылкой символа `00` по UART.
`0xAA`/`0x55` — это контроль процесса начала сеанса для исследованного HFM, **не** безусловное утверждение о любой системе управления Mercedes. Не принимать смешанные настройки `8N1` и `8S1` как стандартный OBD-II.
## 5. Модель транзакции HFM и автоматы состояний

Семантическая форма запроса `(CMD, PARAM)`: передать CMD, принять эхо CMD и дополнение `CMD XOR FF`, затем передать PARAM и принять эхо PARAM и байт DATA. Некоторые встроенные HHT-операции используют однобайтовый селектор в сохранённом контексте страницы. Для операций записи ответ на аргумент может проверяться как дополнение аргумента, а не как произвольное DATA. Контекст и режим управляющего обмена нельзя переносить между ЭБУ без проверки.
```text
DISCONNECTED
  → USB_ATTACHED / power + descriptor check
  → PORT_CONFIGURED / 9600 + timeouts
  → BREAK_ASSERTED (177 ms on known HFMreader path)
  → POST_BREAK (214 ms)
  → SYNCHRONIZE / send AA, expect echo AA + ECU 55
  → SESSION_READY
  → TX_PREFIX (none) → RX_ECHO + RX_ACK(~PREFIX)
  → TX_SELECTOR (space) → RX_ECHO + RX_DATA
  → DECODE / check variant, context, range, freshness
  → SESSION_READY
  → on failure: log raw + reset session; DO NOT AUTO-RETRY WRITE
```

Отрицательный результат синхронизации, ошибочный ACK, тайм-аут, смена диагностического индекса или невозможность разобрать ответ переводят транспорт в `ERROR` и требуют нового подтверждённого сеанса. Не выполнять сканирование всех 256 команд на реальном ЭБУ: неизвестные адреса могут писать конфигурацию.
## 6. Полная объединённая номенклатура команд

### 6.1 HFMScan: все записи каталога запросов

| CMD | PARAM | Семантика | Режим | Ответ/детали | Адрес Ghidra | Риск |
| --- | --- | --- | --- | --- | --- | --- |
| 0xAA | — | wake-up / sync byte | init | 0x55 (= 0xFF-0xAA) | 0x4935a4 | Чтение/служебная |
| 0x10 | 0x00 | keep-alive (IDLE_CMD) | service | sent when no exchange for IDLE_TIME ms (hfmscan.ini IDLE_TIME=1300, IDLE_CMD=16 0). 0x10 is also used as a data-read command with other PARAM values (see live data). | 0x49c24c, 0x49c2ec | Чтение/служебная |
| 0xF0 | 0x00..0x27 (0x1C..0x25 skipped) | read identification byte | read | one byte of the identification block; layout in ecu_profiles.json | 0x498234 | Чтение/служебная |
| 0xF9 | 0x00..0x0D | read VIN byte | read | 14 ASCII bytes; read stops if byte 0 is 0x00 or 0xFF. A 100 ms pause precedes (F9,00). | 0x498234 (loop @0x49951b), caption table 0x4dfef8 'VIN: ' | Чтение/служебная |
| 0xF1 | fault-memory byte address | read fault memory byte (HFM/PMS) | read | bytes 0x00-0x0F = current faults bitmap, 0x10-0x1F = stored faults bitmap; extended adds 0x30-0x32 / 0x40-0x42 for bits 129..152. Re-read every 120 s during polling. | 0x4a67f0 (returns 0xF1), 0x4a36c4, 0x4a2df8, 0x4a35d4 | Чтение/служебная |
| 0x30 | 0x01..0x1E | read fault memory byte (LH, system type 3) | read | — | 0x4a67f0, 0x4a35d4 | Чтение/служебная |
| 0x02 | page = ID[0x26] | select ROM page for descriptor table | read-setup | DATA == 0xFF - page | 0x4a04c8 (state 1/2) | Чтение/служебная |
| 0x80 | address | read byte of selected ROM page | read | used to dump the live-data descriptor table starting at ID[0x27]; length = 2*0x1B (diag 40..44) or 2*0x61 (diag 45..59) for HFM, 2*0x1B for PMS diag 60..79 | 0x4a04c8, 0x4a52e8 | Чтение/служебная |
| 0x40 | 0xF7, 0xF8, 0xFA, 0xFB | LH extra identification reads | read | — | 0x498234 (type 3 branch) | Чтение/служебная |
| 0x10/0x12/0x13/0x20/0x21/0x22/0x23/0x24/0x27 | address | live-data read (memory area selected by CMD) | read | exact (CMD,PARAM) per parameter comes from the ECU's ROM descriptor table or from hard-coded maps in 0x49dbf0; see live_data.csv | 0x49c534, 0x49cad0, 0x49dbf0 | Чтение/служебная |
| 0xF3 | 0xFF | erase fault memory (HFM/PMS) | write | — | 0x4a3828 (state 7, from btnEraseErrClick 0x4cd254 -> 0x4a3bd4) | ЗАПИСЬ — ЗАПРЕЩЕНА |

Источники: `HFMScan_REPORT/protocol/commands.json` и соответствующие `evidence/c_*.c`, особенно `c_4942c8_4952f0.c`, `c_4a04c8_raw_scan.c`, `c_498234_read_faults.c`, `c_4cd254_btnEraseErr.c`. Модуль ELM327 (c)HfmScan и специальный ISO-транспорт других ЭБУ существуют, но к ядру HFM для ESP32-S3 не относятся.
### 6.2 HFMreader: все описанные семантические команды

| Операция | Байты/схема | Чётность | ACK/ответ | Назначение | Статус | Доказательство |
| --- | --- | --- | --- | --- | --- | --- |
| PREFIX_READ_PAGE_20 | 20 | none | DF (= 20 XOR FF) | prefix/opcode | CS | hfmreader.field_protocol FieldHFMProtocol.read_group L38-61 @bc+72..512; hfmreader.protocol HFMProtocol.read_parameter L60-73 @bc+168..546 |
| PREFIX_READ_PAGE_21 | 21 | none | DE | prefix/opcode | CS | hfmreader.field_profiles <module> L72-127 @bc+172..366 (OBSERVED_GROUPS, PROFILES) |
| PREFIX_PAGE_24 | 24 | none | DB | prefix (adaptation/config page) | CS | hfmreader.hht_control.recipes <module> L11-24 @bc+20..44; hfmreader.hht_control.configuration decode_configuration L17 @bc+88..118 (216,219) |
| PREFIX_PAGE_27 | 27 | none | D8 | prefix (coding page) | CS | hfmreader.hht_control.configuration decode_configuration L17 @bc+88..118 |
| ECU_IDENTIFICATION | 28 × [F0 (none) -> ack 0F ; index 00..1B (space) -> data byte] | — | — | — | CS | hfmreader.ecu_identification_protocol EcuIdentificationProtocol.read L23-33 @bc+10..318; hfmreader.ecu_identification parse_ecu_identification L56-70 |
| DTC_READ | for index in 00..1F: F1 (none) -> ack 0E ; index (space) -> fault bitmap byte | — | — | — | CS | hfmreader.dtc_protocol DtcReadOnlyProtocol L41-43 @bc+12..70; scan_indexes L63-79; hfmreader.dtc _status_and_normalized_index L131-136 |
| DTC_CLEAR | — | opcode none, argument space | — | — | CS | hfmreader.dtc_protocol DtcClearProtocol body L91-94 @bc+12..26 (_CLEAR_TRANSACTIONS); hfmreader.dtc_protocol DtcClearProtocol.clear_and_verify L116-132; _transaction L141-149 |
| CONFIG_READ | 27* 02 24* F0 | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['configuration'] L12; hfmreader.hht_control.configuration decode_configuration L14-32 |
| ADAPT_READ_IDLE | 24* 90 | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['idle'] L15 |
| ADAPT_READ_LOWER | 24* 92 | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['lower'] L16 |
| ADAPT_READ_UPPER | 24* 93 | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['upper'] L17 |
| READ_INJECTION_TIME | 20* 54 20* 55 B7 | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['injection'] L13 |
| READ_LAMBDA_CONTROL | B9 B7 (selector-only, needs PREFIX 20 context) | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['lambda'] L14 |
| ADAPT_RESET | CF* FF C1* FC E0* 00 CF* 00 C0* FC | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['reset'] L24; hfmreader.hht_control.session ControlSession._recipe L164-168; _entry_status L304-306; _conditions L260-261; confirm L319-322 |
| CTRL_REMOTE_ENTRY | CF* FF C1* FC | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['remote'] L18; hfmreader.hht_control.session confirm L324-325 |
| CTRL_READY_STATUS | 96 (selector-only) | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['ready'] L19; hfmreader.hht_control.session _ready L294-301 |
| CTRL_SONDE | D0* 0F | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['sonde'] L20 |
| CTRL_PURGE_ON | D4* 1A | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['purge_on'] L21; hfmreader.hht_control.session _conditions L262-263; purge L333-340; tick L352-355 |
| CTRL_PURGE_OFF | D4* 00 | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['purge_off'] L22 |
| CTRL_RESTORE | D0* 0B D1* 00 D2* 00 D3* 40 D4* 00 (+ D0* 0F again if sonde was sent) | — | — | — | CS | hfmreader.hht_control.recipes RECIPES['restore'] L23; hfmreader.hht_control.session restore L342-350 |
| PN_CANDIDATE_READ | 20* 23 99 68 21* D7 BF 69 98 67 | — | — | — | AC | hfmreader.pn_candidate <module> L10 @bc+24 RECIPE; read_candidate L15-25 |

### 6.3 Адреса и назначение селекторов

| Страница | Селектор | Значение | Использование | Достоверность |
| --- | --- | --- | --- | --- |
| 20 | 23 | Upstream O2 sensor voltage (native ×4 mV; HHT formula 5.1102·raw−192.83 mV) | mixture_iat; HFM_R008/R044 | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| 20 | 25 | Downstream O2 sensor voltage (HHT R046) | HFM_R046 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | 47 | Vehicle speed raw (1.2·raw km/h) | HFM_R043/R172 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | 48 | Air-mass low byte (W, little-endian with 0x49) | air_mass_low_candidate; HFM_R007 | APP_FIELD_VERIFIED (author's registry marks VERIFIED on ECU 013 545 13 32; not re-tested here) |
| 20 | 49 | Air-mass high byte | air_mass_high_candidate; airflow[0]; HFM_R007 | APP_FIELD_VERIFIED (author's registry marks VERIFIED on ECU 013 545 13 32; not re-tested here) |
| 20 | 54 | Injection time low byte (with 0x55: ((lo+256·hi)//50)/10 ms); NOT rpm (app: disproven_as_rpm) | rpm_low; hht_control injection | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | 55 | Injection time high byte | rpm_high_context; hht_control injection | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | 67 | Idle target speed raw (25·raw rpm) when read after PREFIX 20 (HHT llupm, I6A≥0x41) | HFM_R002/R028 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | 68 | Status bits: deceleration fuel cut-off (mask 0x04) [HHT context mixture_iat] | HFM_R026 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | 98 | Status bits: starter T50 (mask 0x80), idle contact (mask 0x80, guessed), P/N vs D/R candidate (mask 0x20) | HFM_R015/R022; pn_candidate | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | 99 | Status bits: A/C compressor (mask 0x01); NOT IAT (app: disproven_selector_99_as_iat) | mixture_iat[1]; HFM_R021 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | B7 | Engine speed (25·raw rpm) | airflow[2]; HFM_R006/R031/R168; validity gate | APP_FIELD_VERIFIED (author's registry marks VERIFIED on ECU 013 545 13 32; not re-tested here) |
| 20 | B9 | Lambda control (0.390625·raw − 50 %), read after B9 B7 recipe | airflow[5]; rpm_high_context; hht_control lambda | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 20 | BD | Throttle angle (raw·90/256 °) | airflow[1]; HFM_R005/R039 | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| 20 | C2 | Engine load (raw/2 %) | airflow[3] | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| 20 | C4 | Ignition angle (55 − raw·90/256 °CA) | airflow[4]; HFM_R012 | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| 21 | B7 | Engine speed (same value as 20:B7 observed) | coolant[2] | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| 21 | BE | Intake-air temperature (8-point candidate / HHT 256-entry table) | coolant[3]; HFM_R013 | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| 21 | BF | Coolant temperature (14-point piecewise / HHT 256-entry table) | coolant[1]; HFM_R001/R045/R062 | APP_FIELD_VERIFIED (author's registry marks VERIFIED on ECU 013 545 13 32; not re-tested here) |
| 21 | D7 | Idle target alternative (llupm when I6A<0x41); unknown otherwise | coolant[0]; HFM_R002/R028 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 21 | 67 | Status bits: intake preheating 0x08, secondary air 0x02, camshaft 0x10, resonance flap 0x04 [HHT context coolant] | coolant[5]; HFM_R018/R019/R024/R025 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 21 | 69 | Status bits: post-start enrichment 0x08, warm-up 0x04, full-load contact 0x40 [HHT context coolant] | coolant[4]; HFM_R016/R017/R023 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 24 | 90 | Idle mixture adaptation (trunc((275·raw−35000)/1000)/10 kg/h) | hht_control idle | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 24 | 92 | Lower part-load mixture adaptation factor | hht_control lower | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 24 | 93 | Upper part-load mixture adaptation factor | hht_control upper | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 24 | F0 | Configuration 'not-applicable' mask byte m | hht_control configuration | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| 27 | 02 | Configuration/coding byte c | hht_control configuration | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |

Полнота селекторов ограничена реальными находками. Указанные адреса без полного префикса НЕ являются самостоятельным разрешением на их передачу. Для `B7`, `B9`, `96` и других коротких рецептов важно состояние текущей страницы HHT.
## 7. Параметры реального времени HFMreader

### 7.1 Native/field/live records — все экспортированные строки

Извлечено **42 записей** таблицы экспорта; некоторые являются вариантами одного измерения, а не независимыми датчиками.
| Ключ | Наименование | Маршрут | CMD | Адрес/рецепт | RAW | Формула | Ед. | Условия | Статус |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| engine_speed | Engine speed | native_group | 20 | B7 | 1 | rpm = 25 * raw | rpm | - | APP_FIELD_VERIFIED (author's registry marks VERIFIED on ECU 013 545 13 32; not re-tested here) |
| coolant_temperature | Coolant temperature | native_group | 21 | BF (group coolant: D7 BF B7 BE 69 67) | 1 | linear interpolation over COOLANT_POINTS, clamped at ends | °C | - | APP_FIELD_VERIFIED (author's registry marks VERIFIED on ECU 013 545 13 32; not re-tested here) |
| o2_sensor_voltage | O2 sensor voltage (upstream) | native_group | 20 | 23 | 1 | mV = 4 * raw | mV | - | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| throttle_angle | Throttle angle | native_group | 20 | BD | 1 | deg = raw * 90 / 256 | ° | - | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| engine_load | Engine load | native_group | 20 | C2 | 1 | % = raw / 2 | % | - | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| ignition_angle | Ignition angle | native_group | 20 | C4 | 1 | deg = 55 - raw * 90 / 256  (piecewise (0,55)->(256,-35)) | °CA | - | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| air_mass | Air mass flow (HFM) | native_group | 20 | 48 (low) + 49 (high), read in separate prefix groups | 2 | kg/h = (lo \| hi << 8) / 10 ; lo = 20:48, hi = 20:49 (little-endian) | kg/h | MAF bytes must be sampled together | APP_FIELD_VERIFIED (author's registry marks VERIFIED on ECU 013 545 13 32; not re-tested here) |
| intake_air_temperature | Intake-air temperature | native_group | 21 | BE | 1 | linear interpolation over IAT_POINTS; raw outside [92.16, 230.4] -> no value | °C | raw within 92.16..230.4 | APP_HIGHLY_LIKELY (author's evidence level; not vehicle-proven) |
| injection_time | Injection time | hht_control_recipe | 20/20 | 20* 54 20* 55 B7 | responses incl. prefix acks | ms = ((r1 + 256 * r3) // 50) / 10 ; invalid if r4 (B7) == 0 | ms | trailing B7 != 0 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| lambda_control | Lambda control | hht_control_recipe | 20 (context) | B9 B7 | responses incl. prefix acks | % = 390625 * r0 // 1000000 - 50 ; invalid if r1 (B7) == 0 | % | trailing B7 != 0 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| adapt_idle | Idle mixture adaptation (additive) | hht_control_recipe | 24 | 24* 90 | responses incl. prefix acks | kg/h = trunc0(275 * r1 - 35000, 1000) / 10 | kg/h | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| adapt_lower | Lower part-load adaptation (multiplicative) | hht_control_recipe | 24 | 24* 92 | responses incl. prefix acks | factor = (trunc0(390625 * r1 - 50000000, 1000000) + 100) / 100 | factor | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| adapt_upper | Upper part-load adaptation (multiplicative) | hht_control_recipe | 24 | 24* 93 | responses incl. prefix acks | factor = (trunc0(390625 * r1 - 50000000, 1000000) + 100) / 100 | factor | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R001 | Coolant temperature | hht_experimental_recipe | per recipe | BF (after group coolant) | 1 | °C = TABLES.coolant[d0] | °C | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R002 | Engine speed / idle target | hht_experimental_recipe | per recipe | 21* D7 20* 67 B7 | 5 | word0 = (25 * (d2 if I6A < 0x41 else d6)) if d8 else 0 ; word1 = 25 * d8 | rpm | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R005 | Throttle angle | hht_experimental_recipe | per recipe | BD (after group airflow) | 1 | word = 3515625 * d0 // 1000000 ; deg = word / 10 | ° | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R006 | Engine speed | hht_experimental_recipe | per recipe | B7 (after group airflow) | 1 | rpm = 25 * d0 | rpm | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R007 | Air mass flow | hht_experimental_recipe | per recipe | 20* 48 20* 49 B7 | 5 | kg/h = (d2 + 256 * d6) // 10 | kg/h | gate raw[8] != 0 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R008 | Upstream oxygen voltage | hht_experimental_recipe | per recipe | 20* 23 B7 | 3 | mV = trunc0(51102 * d2 - 1928300, 10000) | mV | gate raw[4] != 0 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R012 | Ignition angle | hht_experimental_recipe | per recipe | C4 B7 (after group airflow) | 2 | 0 if 25 * d2 < 100 else trunc0(5500000 - 35157 * d0, 100000) | °CA;°C | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R013 | Intake temperature | hht_experimental_recipe | per recipe | BE (after group coolant) | 1 | °C = TABLES.intake_air[d0] | °C | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R015 | Starter signal | hht_experimental_recipe | per recipe | 98 (after group airflow) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=1: mask=0x40 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R016 | Post-start enrichment | hht_experimental_recipe | per recipe | 69 (after group coolant) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=258: mask=0x08 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R017 | Warm-up status | hht_experimental_recipe | per recipe | 69 (after group coolant) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=267: mask=0x04 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R018 | Intake preheating | hht_experimental_recipe | per recipe | 67 (after group coolant) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=5: mask=0x08 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R019 | Secondary air | hht_experimental_recipe | per recipe | 67 (after group coolant) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=3: mask=0x02 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R021 | A/C compressor | hht_experimental_recipe | per recipe | 99 (after group mixture_iat) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=8: mask=0x01 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R022 | Idle contact | hht_experimental_recipe | per recipe | 98 (after group airflow) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=0: mask=0x80 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R023 | Full-load contact | hht_experimental_recipe | per recipe | 69 (after group coolant) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=9: mask=0x40 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R024 | Camshaft adjustment | hht_experimental_recipe | per recipe | 67 (after group coolant) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=6: mask=0x10 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R025 | Resonance intake | hht_experimental_recipe | per recipe | 67 (after group coolant) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=4: mask=0x04 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R026 | Deceleration fuel cut | hht_experimental_recipe | per recipe | 68 (after group mixture_iat) | 1 | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) [q=10: mask=0x04 invert=False] | ON/OFF | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R028 | Engine speed / idle target | hht_experimental_recipe | per recipe | 21* D7 20* 67 B7 | 5 | word0 = (25 * (d2 if I6A < 0x41 else d6)) if d8 else 0 ; word1 = 25 * d8 | rpm | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R031 | Idle speed candidate | hht_experimental_recipe | per recipe | B7 (after group airflow) | 1 | rpm = 25 * d0 | rpm | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R039 | Throttle angle candidate | hht_experimental_recipe | per recipe | BD (after group airflow) | 1 | word = 3515625 * d0 // 1000000 ; deg = word / 10 | ° | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R043 | Rear vehicle speed | hht_experimental_recipe | per recipe | 20* 47 | 2 | km/h = 12 * d2 // 10 | km/h | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R044 | Oxygen voltage candidate | hht_experimental_recipe | per recipe | 20* 23 B7 | 3 | mV = trunc0(51102 * d2 - 1928300, 10000) | mV | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R045 | Coolant candidate | hht_experimental_recipe | per recipe | BF (after group coolant) | 1 | °C = TABLES.coolant[d0] | °C | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R046 | Downstream oxygen voltage | hht_experimental_recipe | per recipe | 20* 25 B7 | 3 | mV = trunc0(51102 * d2 - 1928300, 10000) | mV | gate raw[4] != 0 | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R062 | Start-temperature candidate | hht_experimental_recipe | per recipe | BF (after group coolant) | 1 | °C = TABLES.coolant[d0] | °C | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R168 | Engine speed candidate | hht_experimental_recipe | per recipe | B7 (after group airflow) | 1 | rpm = 25 * d0 | rpm | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |
| hht_HFM_R172 | Vehicle speed candidate | hht_experimental_recipe | per recipe | 20* 47 | 2 | km/h = 12 * d2 // 10 | km/h;V | - | APP_CANDIDATE (author marks candidate/conditional/unvalidated) |

### 7.2 Все 29 рецептов из HFMreader 1.1.1

Значение `*` в обозначении префикса — контекстный CMD с режимом NOPARITY. Буквальные рецепты HFMreader требуют контекста предварительно открытой группы; индексы ответов в двухбайтовом промежуточном буфере отличаются от индексов UART-кадров.
| ID | Название | Единицы | Рецепт (*CMD) | Преамбула | RAW | Смещения | Колбэк | Формула | Период/устар. | Статус |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HFM_R001 | Coolant temperature | °C | BF | coolant | 2 | 0 | _hfm_calc_tkm | °C = TABLES.coolant[d0] | 5/30 | NEEDS_RUNTIME_VALIDATION |
| HFM_R002 | Engine speed / idle target | rpm | 21* D7 20* 67 B7 | none (body has prefix) | 10 | 2 6 8 | _hfm_calc_llupm | word0 = (25 * (d2 if I6A < 0x41 else d6)) if d8 else 0 ; word1 = 25 * d8 | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R005 | Throttle angle | ° | BD | airflow | 2 | 0 | _hfm_calc_dkw | word = 3515625 * d0 // 1000000 ; deg = word / 10 | 1/10 | NEEDS_RUNTIME_VALIDATION |
| HFM_R006 | Engine speed | rpm | B7 | airflow | 2 | 0 | _hfm_calc_upm | rpm = 25 * d0 | 1/10 | NEEDS_RUNTIME_VALIDATION |
| HFM_R007 | Air mass flow | kg/h | 20* 48 20* 49 B7 | none (body has prefix) | 10 | 2 6 | _hfm_calc_luma | kg/h = (d2 + 256 * d6) // 10 | 2/10 | NEEDS_RUNTIME_VALIDATION |
| HFM_R008 | Upstream oxygen voltage | mV | 20* 23 B7 | none (body has prefix) | 6 | 2 | _hfm_calc_o2 | mV = trunc0(51102 * d2 - 1928300, 10000) | 2/10 | NEEDS_RUNTIME_VALIDATION |
| HFM_R012 | Ignition angle | °CA;°C | C4 B7 | airflow | 4 | 0 2 | _hfm_calc_zuendw | 0 if 25 * d2 < 100 else trunc0(5500000 - 35157 * d0, 100000) | 2/10 | NEEDS_RUNTIME_VALIDATION |
| HFM_R013 | Intake temperature | °C | BE | coolant | 2 | 0 | _hfm_calc_tal | °C = TABLES.intake_air[d0] | 5/30 | NEEDS_RUNTIME_VALIDATION |
| HFM_R015 | Starter signal |  | 98 | airflow | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 2/10 | DISPLAY_ONLY_OR_STATUS |
| HFM_R016 | Post-start enrichment |  | 69 | coolant | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 5/30 | DISPLAY_ONLY_OR_STATUS |
| HFM_R017 | Warm-up status |  | 69 | coolant | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 5/30 | DISPLAY_ONLY_OR_STATUS |
| HFM_R018 | Intake preheating |  | 67 | coolant | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 10/60 | DISPLAY_ONLY_OR_STATUS |
| HFM_R019 | Secondary air |  | 67 | coolant | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 5/30 | DISPLAY_ONLY_OR_STATUS |
| HFM_R021 | A/C compressor |  | 99 | mixture_iat | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 5/30 | DISPLAY_ONLY_OR_STATUS |
| HFM_R022 | Idle contact |  | 98 | airflow | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 1/10 | DISPLAY_ONLY_OR_STATUS |
| HFM_R023 | Full-load contact |  | 69 | coolant | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 1/10 | DISPLAY_ONLY_OR_STATUS |
| HFM_R024 | Camshaft adjustment |  | 67 | coolant | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 5/30 | DISPLAY_ONLY_OR_STATUS |
| HFM_R025 | Resonance intake |  | 67 | coolant | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 5/30 | DISPLAY_ONLY_OR_STATUS |
| HFM_R026 | Deceleration fuel cut |  | 68 | mixture_iat | 2 | 0 | _hfm_calc_bit | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | 1/10 | DISPLAY_ONLY_OR_STATUS |
| HFM_R028 | Engine speed / idle target | rpm | 21* D7 20* 67 B7 | none (body has prefix) | 10 | 2 6 8 | _hfm_calc_llupm | word0 = (25 * (d2 if I6A < 0x41 else d6)) if d8 else 0 ; word1 = 25 * d8 | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R031 | Idle speed candidate | rpm | B7 | airflow | 2 | 0 | _hfm_calc_upm | rpm = 25 * d0 | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R039 | Throttle angle candidate | ° | BD | airflow | 2 | 0 | _hfm_calc_dkw | word = 3515625 * d0 // 1000000 ; deg = word / 10 | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R043 | Rear vehicle speed | km/h | 20* 47 | none (body has prefix) | 4 | 2 | _hfm_calc_kmh | km/h = 12 * d2 // 10 | 1/10 | NEEDS_RUNTIME_VALIDATION |
| HFM_R044 | Oxygen voltage candidate | mV | 20* 23 B7 | none (body has prefix) | 6 | 2 | _hfm_calc_o2 | mV = trunc0(51102 * d2 - 1928300, 10000) | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R045 | Coolant candidate | °C | BF | coolant | 2 | 0 | _hfm_calc_tkm | °C = TABLES.coolant[d0] | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R046 | Downstream oxygen voltage | mV | 20* 25 B7 | none (body has prefix) | 6 | 2 | _hfm_calc_o2 | mV = trunc0(51102 * d2 - 1928300, 10000) | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R062 | Start-temperature candidate | °C | BF | coolant | 2 | 0 | _hfm_calc_tkm | °C = TABLES.coolant[d0] | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R168 | Engine speed candidate | rpm | B7 | airflow | 2 | 0 | _hfm_calc_upm | rpm = 25 * d0 | 10/60 | NEEDS_RUNTIME_VALIDATION |
| HFM_R172 | Vehicle speed candidate | km/h;V | 20* 47 | none (body has prefix) | 4 | 2 | _hfm_calc_kmh | km/h = 12 * d2 // 10 | 10/60 | NEEDS_RUNTIME_VALIDATION |

Байт-код: `hfmreader.hht_experimental.recipe_executor`, `decoder`, `scheduler`; источник примеров: `candidates.json` из поставки. `raw[2*i]` содержит i-й ответ, нечётные индексы — заполнитель структуры. Флаги доступности «engine running», variant / diagnostic-index обязаны проверяться.
## 8. Формулы и точные температурные таблицы

### 8.1 Все восстановленные и вычислительные формулы

| ID | Величина | Входные данные | Формула | Ед. | Класс | Место в исходниках |
| --- | --- | --- | --- | --- | --- | --- |
| multiply_25 | engine_speed | 20:B7 (1 byte) | rpm = 25 * raw | rpm | AV | <lambda> L232 @bc+2..10; engine_speed def L200-232 |
| hfmscan_coolant_piecewise_14_point | coolant_temperature | 21:BF (1 byte) | linear interpolation over COOLANT_POINTS, clamped at ends | °C | AV | COOLANT_POINTS L169 @bc+368; _piecewise.convert L142-150 |
| multiply_4 | o2_sensor_voltage | 20:23 | mV = 4 * raw | mV | AH | <lambda> L294 |
| scale_90_over_256 | throttle_angle | 20:BD | deg = raw * 90 / 256 | ° | AH | <lambda> L324 |
| divide_2 | engine_load | 20:C2 | % = raw / 2 | % | AH | <lambda> L354 |
| linear_55_to_minus_35 | ignition_angle | 20:C4 | deg = 55 - raw * 90 / 256  (piecewise (0,55)->(256,-35)) | °CA | AH | ignition_angle converter L384 @bc+2622..2628 |
| divide_10 | air_mass | 20:48 + 20:49 | kg/h = (lo \| hi << 8) / 10 ; lo = 20:48, hi = 20:49 (little-endian) | kg/h | AV | <lambda> L436; air_mass def L386-436; decode_snapshot L215-225 |
| mercedes_b17_adc_piecewise_8_point_candidate | intake_air_temperature | 21:BE | linear interpolation over IAT_POINTS; raw outside [92.16, 230.4] -> no value | °C | AH | IAT_CANDIDATE_POINTS L187 @bc+372; _bounded_piecewise L155-164 |
| hht_tkm | — | — | °C = TABLES.coolant[d0] | °C | AC | _words L43-44 |
| hht_tal | — | — | °C = TABLES.intake_air[d0] | °C | AC | _words L45-46 |
| hht_upm | — | — | rpm = 25 * d0 | rpm | AC | _words L47-48 |
| hht_llupm | — | — | word0 = (25 * (d2 if I6A < 0x41 else d6)) if d8 else 0 ; word1 = 25 * d8 | rpm | AC | _words L49-51 |
| hht_dkw | — | — | word = 3515625 * d0 // 1000000 ; deg = word / 10 | ° | AC | _words L52-53 |
| hht_luma | — | — | kg/h = (d2 + 256 * d6) // 10 | kg/h | AC | _words L54-55 |
| hht_o2 | — | — | mV = trunc0(51102 * d2 - 1928300, 10000) | mV | AC | _words L56-57 |
| hht_zuendw | — | — | 0 if 25 * d2 < 100 else trunc0(5500000 - 35157 * d0, 100000) | °CA | AC | _words L58-59 |
| hht_kmh | — | — | km/h = 12 * d2 // 10 | km/h | AC | _words L60-61 |
| hht_bit | — | — | active = (d0 & bit_v1_old[q & 0xFF]) != 0 ; if (q >> 8) & 2: active = not active ; word = 2 (ON) if active else 1 (OFF) | ON/OFF | AC | _words L62-67 |
| trunc0 | — | — | trunc0(n, d) = sign(n) * (abs(n) // d)  (truncate toward zero) | — | CS | trunc0 L37-38 |
| fuel_injection | — | 20*54 20*55 B7 responses | ms = ((r1 + 256 * r3) // 50) / 10 ; invalid if r4 (B7) == 0 | ms | AC | decode_values L41-43 |
| fuel_lambda | — | B9 B7 responses | % = 390625 * r0 // 1000000 - 50 ; invalid if r1 (B7) == 0 | % | AC | decode_values L44 |
| adapt_idle | — | 24* 90 | kg/h = trunc0(275 * r1 - 35000, 1000) / 10 | kg/h | AC | decode_values L45 |
| adapt_partload | — | 24* 92 / 24* 93 | factor = (trunc0(390625 * r1 - 50000000, 1000000) + 100) / 100 | factor | AC | decode_values L46 |
| coolant_plausibility | — | — | suspect if v < -40 or v > 130 or (\|v - last\| > 15 within 0..5 s) | °C | CS | CoolantQuality.check L5-11 |

`trunc0` — целочисленное деление с усечением к нулю (не Python `//` для отрицательных чисел). Для температур и адаптаций важны насыщение, знак, невозможные/замещающие значения, а также входная валидация по идентификатору ЭБУ.
### 8.2 Точные таблицы температур, индексы 0x00–0xFF

Ресурс: `HFMreader_REPORT_1/HFMreader_REPORT_1/extracted/release_zip/.../hht_experimental/candidates.json`; ключи: coolant, intake_air, bit_v0_old, bit_v1_old, bit_new_mask, bit_new_polarity.
**coolant:** размер 256; единица °C; `temperature = values[raw]`.
| RAW HEX | Значения для +0 … +15 |
| --- | --- |
| 00–0F | 00=-59; 01=-52; 02=-46; 03=-42; 04=-39; 05=-36; 06=-34; 07=-31; 08=-29; 09=-27; 0A=-26; 0B=-24; 0C=-23; 0D=-21; 0E=-20; 0F=-19 |
| 10–1F | 10=-17; 11=-16; 12=-15; 13=-14; 14=-13; 15=-12; 16=-11; 17=-10; 18=-9; 19=-8; 1A=-7; 1B=-7; 1C=-6; 1D=-5; 1E=-4; 1F=-3 |
| 20–2F | 20=-3; 21=-2; 22=-1; 23=-1; 24=0; 25=1; 26=2; 27=2; 28=3; 29=4; 2A=4; 2B=5; 2C=5; 2D=6; 2E=7; 2F=7 |
| 30–3F | 30=8; 31=8; 32=9; 33=9; 34=10; 35=11; 36=11; 37=12; 38=12; 39=13; 3A=13; 3B=14; 3C=14; 3D=15; 3E=15; 3F=16 |
| 40–4F | 40=16; 41=17; 42=17; 43=18; 44=18; 45=19; 46=19; 47=20; 48=20; 49=21; 4A=21; 4B=22; 4C=22; 4D=23; 4E=23; 4F=23 |
| 50–5F | 50=24; 51=24; 52=25; 53=25; 54=26; 55=26; 56=27; 57=27; 58=28; 59=28; 5A=28; 5B=29; 5C=29; 5D=30; 5E=30; 5F=31 |
| 60–6F | 60=31; 61=32; 62=32; 63=32; 64=33; 65=33; 66=34; 67=34; 68=35; 69=35; 6A=35; 6B=36; 6C=36; 6D=37; 6E=37; 6F=38 |
| 70–7F | 70=38; 71=39; 72=39; 73=39; 74=40; 75=40; 76=41; 77=41; 78=42; 79=42; 7A=42; 7B=43; 7C=43; 7D=44; 7E=44; 7F=45 |
| 80–8F | 80=45; 81=46; 82=46; 83=47; 84=47; 85=47; 86=48; 87=48; 88=49; 89=49; 8A=50; 8B=50; 8C=51; 8D=51; 8E=52; 8F=52 |
| 90–9F | 90=53; 91=53; 92=54; 93=54; 94=54; 95=55; 96=55; 97=56; 98=57; 99=57; 9A=57; 9B=58; 9C=58; 9D=59; 9E=59; 9F=60 |
| A0–AF | A0=61; A1=61; A2=62; A3=62; A4=63; A5=63; A6=64; A7=64; A8=65; A9=65; AA=66; AB=67; AC=67; AD=68; AE=68; AF=69 |
| B0–BF | B0=69; B1=70; B2=71; B3=71; B4=72; B5=73; B6=73; B7=74; B8=74; B9=75; BA=76; BB=76; BC=77; BD=78; BE=79; BF=79 |
| C0–CF | C0=80; C1=81; C2=81; C3=82; C4=83; C5=84; C6=85; C7=85; C8=86; C9=87; CA=88; CB=89; CC=90; CD=91; CE=91; CF=92 |
| D0–DF | D0=93; D1=94; D2=95; D3=96; D4=97; D5=98; D6=99; D7=101; D8=102; D9=103; DA=104; DB=105; DC=107; DD=108; DE=109; DF=111 |
| E0–EF | E0=112; E1=114; E2=115; E3=117; E4=119; E5=120; E6=122; E7=124; E8=126; E9=128; EA=131; EB=133; EC=135; ED=138; EE=141; EF=144 |
| F0–FF | F0=147; F1=151; F2=154; F3=159; F4=163; F5=168; F6=174; F7=181; F8=189; F9=198; FA=210; FB=232; FC=255; FD=277; FE=300; FF=300 |

**intake_air:** размер 256; единица °C; `temperature = values[raw]`.
| RAW HEX | Значения для +0 … +15 |
| --- | --- |
| 00–0F | 00=-40; 01=-40; 02=-40; 03=-40; 04=-40; 05=-40; 06=-40; 07=-40; 08=-40; 09=-39; 0A=-37; 0B=-36; 0C=-34; 0D=-33; 0E=-31; 0F=-30 |
| 10–1F | 10=-29; 11=-28; 12=-27; 13=-26; 14=-26; 15=-25; 16=-24; 17=-23; 18=-22; 19=-21; 1A=-20; 1B=-19; 1C=-19; 1D=-18; 1E=-18; 1F=-17 |
| 20–2F | 20=-16; 21=-16; 22=-15; 23=-14; 24=-14; 25=-13; 26=-13; 27=-12; 28=-11; 29=-11; 2A=-10; 2B=-10; 2C=-9; 2D=-9; 2E=-8; 2F=-8 |
| 30–3F | 30=-7; 31=-7; 32=-7; 33=-6; 34=-6; 35=-5; 36=-5; 37=-4; 38=-4; 39=-4; 3A=-3; 3B=-3; 3C=-2; 3D=-2; 3E=-1; 3F=-1 |
| 40–4F | 40=0; 41=0; 42=0; 43=1; 44=1; 45=2; 46=2; 47=2; 48=3; 49=3; 4A=4; 4B=4; 4C=4; 4D=5; 4E=5; 4F=5 |
| 50–5F | 50=6; 51=6; 52=7; 53=7; 54=7; 55=8; 56=8; 57=9; 58=9; 59=9; 5A=10; 5B=10; 5C=10; 5D=11; 5E=11; 5F=11 |
| 60–6F | 60=12; 61=12; 62=12; 63=13; 64=13; 65=13; 66=14; 67=14; 68=15; 69=15; 6A=15; 6B=16; 6C=16; 6D=16; 6E=17; 6F=17 |
| 70–7F | 70=17; 71=18; 72=18; 73=18; 74=19; 75=19; 76=19; 77=20; 78=20; 79=20; 7A=21; 7B=21; 7C=21; 7D=22; 7E=22; 7F=23 |
| 80–8F | 80=23; 81=23; 82=24; 83=24; 84=24; 85=25; 86=25; 87=25; 88=26; 89=26; 8A=26; 8B=27; 8C=27; 8D=28; 8E=28; 8F=28 |
| 90–9F | 90=29; 91=29; 92=29; 93=30; 94=30; 95=30; 96=31; 97=31; 98=32; 99=32; 9A=33; 9B=33; 9C=33; 9D=34; 9E=34; 9F=35 |
| A0–AF | A0=35; A1=35; A2=36; A3=36; A4=37; A5=37; A6=38; A7=38; A8=38; A9=39; AA=39; AB=40; AC=40; AD=41; AE=41; AF=42 |
| B0–BF | B0=42; B1=43; B2=43; B3=44; B4=44; B5=45; B6=45; B7=46; B8=46; B9=47; BA=47; BB=48; BC=48; BD=49; BE=49; BF=50 |
| C0–CF | C0=50; C1=51; C2=51; C3=52; C4=53; C5=53; C6=54; C7=54; C8=55; C9=56; CA=56; CB=57; CC=58; CD=58; CE=59; CF=59 |
| D0–DF | D0=60; D1=61; D2=62; D3=63; D4=63; D5=64; D6=65; D7=66; D8=67; D9=68; DA=68; DB=69; DC=70; DD=71; DE=72; DF=73 |
| E0–EF | E0=74; E1=76; E2=77; E3=78; E4=79; E5=80; E6=81; E7=83; E8=84; E9=86; EA=87; EB=89; EC=90; ED=92; EE=94; EF=96 |
| F0–FF | F0=98; F1=100; F2=103; F3=107; F4=110; F5=113; F6=117; F7=120; F8=125; F9=130; FA=135; FB=140; FC=150; FD=150; FE=150; FF=150 |

### 8.3 Статусные битовые таблицы HFMreader

**bit_v0_old**
- `values`: 0x40, 0x80, 0x02, 0x08, 0x04, 0x02, 0x01, 0x08, 0x02, 0x20, 0x08, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

**bit_v1_old**
- `values`: 0x80, 0x40, 0x08, 0x02, 0x04, 0x08, 0x10, 0x20, 0x01, 0x40, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

**bit_new_mask**
- `values`: 0x01, 0x08, 0x02, 0x01, 0x02, 0x04, 0x08, 0x02, 0x04, 0x10, 0x08, 0x04, 0x40, 0x40, 0x80, 0x10, 0x20, 0x01, 0x08, 0x10, 0x20, 0x02, 0x04, 0x01, 0x40, 0x20, 0x80, 0x40, 0x40, 0x01, 0x80, 0x01

**bit_new_polarity**
- `values`: 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02

### 8.4 Независимая побайтовая проверка заводской LUT

**Сильное независимое подтверждение:** сравнение всех 256 позиций массива `candidates.json → lookup_tables.coolant.values` из HFMreader 1.1.1 с заводским массивом `HHTwin_REPORT/work/hfm_tables.json → coolant_table_B.values_degC` из `HFMWIN.DLL`, DS:`0xCD82`, показало **256/256 полных совпадений** (0 расхождений). Это подтверждает совпадение таблицы **температуры ОЖ для варианта B HHT** и HFMreader без необходимости интерполяции. Вместе с тем применять эту LUT к другим диагностическим вариантам можно только после проверки выбора соответствующего преобразования в заводской программе.

**Независимое совпадение DTC:** `HFMreader_REPORT_0/dtc/variant_remaps.json → base_numbers` и `HFMScan_REPORT/dtc/dtc_bitmaps.json → T_HFM_0x4dcf38` совпадают по **всем 152 позициям**. Это подтверждает общую базовую карту двух независимых проектов, но **не устраняет** конфликт в назначении текущего/сохранённого банка и условные переназначения вариантов. Кроме того, заводские таблицы HHT-WIN различаются по вариантам B/V и цилиндрам; они не сводятся к одной универсальной карте.

## 9. Расширенные команды Live Data и дескрипторы HFMScan

В HFMScan код `0x4A04C8` выбирает страницу ПЗУ по `F0:26`/`F0:27` и считывает дескрипторы: `02` для переключения страницы, `80` для чтения байта. Длина описаний, согласно отчёту: `2*0x1B` для диагностического индекса 40–44 или `2*0x61` для 45–59; это не значит, что у каждого индекса реально имеются все 97 параметров. Требуется проверка адресной карты конкретного ЭБУ.
Адреса HFMScan `11XX`/`12XX` из `hfmobd.ini` являются внутренней конфигурацией OBD-адаптера и не должны транслироваться в сырые K-Line запросы без декомпиляции пути отправки. Другой адаптер `ELM327 (c)HfmScan` использует команду `HFMDATA`, что не эквивалентно стандартному ELM327.
По Ghidra: `0x49C534` чтение параметра, `0x49DBF0` подбор карты для ECU.bin, `0x49CAD0` заголовок ECU.bin, `0x48A4CC` формулы, `0x487EE4` дескрипторы, `0x4A04C8` чтение ПЗУ.
## 10. Заводские структуры HHT-WIN: профили, тайминги, телеграммы

**Источник третьей реализации:** модуль `HFMWIN.DLL`, сегмент данных 9 и Ghidra-экспорт из HHT-WIN. Формат 16-битных NE DLL. `sg_table_c814.json` представляет внутренние шаблоны HHT, а НЕ напрямую USB-байты. Наличие команды в HHT ещё не доказывает её совместимость с конкретным ЭБУ Bosch.
### 10.1 Таблица режимов времени HHT (at_timings)

| DS | Тип | Скорость | Флаги | T1 | T2 |
| --- | --- | --- | --- | --- | --- |
| 0xbf5e | 5 | 9600.0 | 0x12 | 10 | 10 |
| 0xbf6a | 1 | 0.0 | 0x12 | 10 | 10 |
| 0xbf76 | 2 | 0.0 | 0x12 | 10 | 10 |
| 0xbf82 | 3 | 0.0 | 0x12 | 10 | 10 |
| 0xbf8e | 2 | 0.0 | 0x12 | 500 | 10 |
| 0xbf9a | 4 | 9600.0 | 0x12 | 10 | 10 |

Единицы `T1/T2` не подтверждены декомпиляцией низкоуровневого таймера для всех записей. Значения сохраняются в исходных единицах HHT, не называются автоматически миллисекундами.
### 10.2 Реестр всех 115 AT-телеграмм HHT-WIN

| AT № | Timing-ID | Template-ID | Внутренний HHT шаблон (HEX) |
| --- | --- | --- | --- |
| 0 | 0 | 0 | 00 00 aa 00 |
| 1 | 4 | 1 | 10 01 aa 00 |
| 2 | 2 | 2 | 10 01 00 00 |
| 3 | 3 | 3 | f0 01 00 00 f0 01 01 00 f0 01 02 00 f0 01 03 00 f0 01 04 00 f0 01 05 00 f0 01 06 00 f0 01 07 00 f0 01 08 00 f0 01 09 00 f0 01 0a 00 f0 01 0b 00 f0 01 0c 00 f0 01 0d 00 f0 01 0e 00 f0 01 0f 00 f0 01 10 00 f0 01 11 00 f0 01 12 00 f0 01 13 00 f0 01 14 00 f0 01 15 00 f0 01 16 00 f0 01 17 00 f0 01 18 00 f0 01 19 00 f0 01 1a 00 f0 01 1b 00 f0 01 26 00 f0 01 27 00 |
| 4 | 3 | 4 | f1 01 10 00 f1 01 11 00 f1 01 12 00 f1 01 13 00 f1 01 14 00 f1 01 15 00 f1 01 16 00 f1 01 17 00 f1 01 18 00 f1 01 19 00 f1 01 1a 00 f1 01 1b 00 f1 01 1c 00 f1 01 1d 00 f1 01 1e 00 f1 01 1f 00 |
| 5 | 5 | 0 | 00 00 aa 00 |
| 6 | 2 | 6 | f3 01 ff 00 |
| 7 | 3 | 5 | f1 01 00 00 f1 01 01 00 f1 01 02 00 f1 01 03 00 f1 01 04 00 f1 01 05 00 f1 01 06 00 f1 01 07 00 f1 01 08 00 f1 01 09 00 f1 01 0a 00 f1 01 0b 00 f1 01 0c 00 f1 01 0d 00 f1 01 0e 00 f1 01 0f 00 |
| 8 | 3 | 96 |  |
| 9 | 0 | 0 | 00 00 aa 00 |
| 10 | 2 | 7 | 12 01 76 00 |
| 11 | 1 | 8 | bf 00 |
| 12 | 3 | 9 | 12 01 a4 00 12 01 a5 00 12 01 7e 00 12 01 7e 00 |
| 13 | 3 | 10 | 21 01 d7 00 20 01 67 00 b7 00 |
| 14 | 3 | 11 | 22 01 74 00 22 01 75 00 12 01 7e 00 12 01 7e 00 |
| 15 | 3 | 12 | 20 01 54 00 20 01 55 00 b7 00 |
| 16 | 3 | 13 | 22 01 7f 00 22 01 7f 00 12 01 7e 00 12 01 7e 00 |
| 17 | 3 | 14 | b9 00 b7 00 |
| 18 | 2 | 15 | 22 01 78 00 |
| 19 | 1 | 16 | bd 00 |
| 20 | 3 | 17 | 12 01 7e 00 12 01 7e 00 |
| 21 | 1 | 18 | b7 00 |
| 22 | 3 | 19 | 22 01 72 00 22 01 73 00 12 01 7e 00 12 01 7e 00 |
| 23 | 3 | 20 | 20 01 48 00 20 01 49 00 b7 00 |
| 24 | 3 | 21 | 20 01 f2 00 20 01 f2 00 12 01 7e 00 12 01 7e 00 |
| 25 | 3 | 22 | 20 01 23 00 b7 00 |
| 26 | 3 | 23 | 20 01 43 00 20 01 42 00 |
| 27 | 2 | 24 | 24 01 90 00 |
| 28 | 3 | 25 | 20 01 45 00 20 01 45 00 |
| 29 | 2 | 26 | 24 01 92 00 |
| 30 | 3 | 27 | 20 01 47 00 20 01 47 00 |
| 31 | 2 | 28 | 24 01 93 00 |
| 32 | 3 | 29 | 12 01 72 00 12 01 73 00 12 01 7e 00 12 01 7e 00 |
| 33 | 3 | 30 | c4 00 b7 00 |
| 34 | 3 | 31 | 00 01 7e 00 90 01 34 00 80 00 |
| 35 | 2 | 32 | 20 01 39 00 |
| 36 | 2 | 33 | 23 01 a4 00 |
| 37 | 1 | 34 | 98 00 |
| 38 | 2 | 35 | 23 01 7f 00 |
| 39 | 1 | 36 | 69 00 |
| 40 | 2 | 37 | 23 01 7e 00 |
| 41 | 1 | 38 | 69 00 |
| 42 | 2 | 39 | 12 01 7b 00 |
| 43 | 1 | 40 | 67 00 |
| 44 | 2 | 41 | 22 01 7b 00 |
| 45 | 1 | 42 | 67 00 |
| 46 | 2 | 43 | 12 01 7a 00 |
| 47 | 1 | 44 | 98 00 |
| 48 | 2 | 45 | 12 01 7a 00 |
| 49 | 1 | 46 | 99 00 |
| 50 | 2 | 47 | 12 01 77 00 |
| 51 | 1 | 48 | be 00 |
| 52 | 2 | 49 | 22 01 7a 00 |
| 53 | 1 | 50 | 69 00 |
| 54 | 2 | 51 | 22 01 7a 00 |
| 55 | 1 | 52 | 67 00 |
| 56 | 2 | 53 | 22 01 7a 00 |
| 57 | 1 | 54 | 67 00 |
| 58 | 2 | 55 | 22 01 7a 00 |
| 59 | 1 | 56 | 68 00 |
| 60 | 3 | 57 | 20 01 fc 00 20 01 fc 00 |
| 61 | 2 | 58 | 20 01 41 00 |
| 62 | 2 | 59 | 22 01 7a 00 |
| 63 | 1 | 60 | 98 00 |
| 64 | 3 | 61 | 10 01 02 00 22 01 22 00 10 01 03 00 22 01 23 00 |
| 65 | 3 | 62 | 27 01 02 00 24 01 f0 00 |
| 66 | 3 | 63 | cf 01 ff 00 c1 01 fc 00 |
| 67 | 3 | 64 | cf 01 00 00 c0 01 fc 00 |
| 68 | 2 | 65 | d0 01 0b 00 |
| 69 | 2 | 66 | d0 01 09 00 |
| 70 | 2 | 67 | d0 01 0a 00 |
| 71 | 2 | 68 | d0 01 0b 00 |
| 72 | 2 | 69 | d0 01 2b 00 |
| 73 | 2 | 70 | d0 01 0b 00 |
| 74 | 2 | 71 | d0 01 4b 00 |
| 75 | 3 | 72 | d1 01 00 00 d2 01 00 00 |
| 76 | 3 | 73 | d1 01 01 00 d2 01 80 00 |
| 77 | 2 | 74 | d4 01 00 00 |
| 78 | 2 | 75 | d4 01 1a 00 |
| 79 | 2 | 76 | d2 01 00 00 |
| 80 | 2 | 77 | d2 01 02 00 |
| 81 | 2 | 78 | d2 01 04 00 |
| 82 | 2 | 79 | d2 01 08 00 |
| 83 | 2 | 80 | d2 01 10 00 |
| 84 | 2 | 81 | d2 01 20 00 |
| 85 | 2 | 82 | d2 01 40 00 |
| 86 | 3 | 83 | d1 01 80 00 d3 01 40 00 |
| 87 | 3 | 84 | cf 01 ff 00 c1 01 fc 00 e0 01 00 00 cf 01 00 00 c0 01 fc 00 |
| 88 | 3 | 85 | 10 01 02 00 22 01 22 00 10 01 03 00 22 01 23 00 10 01 04 00 |
| 89 | 3 | 86 | 27 01 02 00 24 01 f0 00 |
| 90 | 3 | 87 | d0 01 0b 00 d1 01 00 00 d2 01 00 00 d3 01 40 00 d4 01 00 00 |
| 91 | 2 | 88 | 12 01 7f 00 |
| 92 | 2 | 89 | 20 01 47 00 |
| 93 | 3 | 90 | e0 01 fe 00 e0 01 ff 00 |
| 94 | 3 | 91 | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 |
| 95 | 2 | 92 | 13 01 2b 00 |
| 96 | 1 | 93 | 96 00 |
| 97 | 2 | 94 | d0 01 0f 00 |
| 98 | 3 | 95 | 20 01 25 00 b7 00 |
| 99 | 3 | 97 | f9 01 00 00 f9 01 01 00 f9 01 02 00 f9 01 03 00 f9 01 04 00 f9 01 05 00 f9 01 06 00 f9 01 07 00 f9 01 08 00 f9 01 09 00 f9 01 0a 00 f9 01 0b 00 f9 01 0c 00 f9 01 0d 00 |
| 100 | 2 | 98 | de 01 02 00 |
| 101 | 3 | 99 | f1 01 10 00 f1 01 11 00 f1 01 12 00 f1 01 13 00 f1 01 14 00 f1 01 15 00 f1 01 16 00 f1 01 17 00 f1 01 18 00 f1 01 19 00 f1 01 1a 00 f1 01 1b 00 f1 01 1c 00 f1 01 1d 00 f1 01 1e 00 f1 01 1f 00 f1 01 40 00 f1 01 41 00 f1 01 42 00 |
| 102 | 3 | 100 | f1 01 00 00 f1 01 01 00 f1 01 02 00 f1 01 03 00 f1 01 04 00 f1 01 05 00 f1 01 06 00 f1 01 07 00 f1 01 08 00 f1 01 09 00 f1 01 0a 00 f1 01 0b 00 f1 01 0c 00 f1 01 0d 00 f1 01 0e 00 f1 01 0f 00 f1 01 30 00 f1 01 31 00 f1 01 32 00 |
| 103 | 2 | 101 | e4 01 00 00 |
| 104 | 2 | 102 | da 01 00 00 |
| 105 | 2 | 103 | db 01 00 00 |
| 106 | 2 | 104 | db 01 04 00 |
| 107 | 3 | 105 | da 01 00 00 db 01 00 00 |
| 108 | 2 | 106 | d2 01 01 00 |
| 109 | 2 | 107 | df 01 72 00 |
| 110 | 2 | 108 | d0 01 03 00 |
| 111 | 2 | 109 | dc 01 80 00 |
| 112 | 2 | 110 | db 01 02 00 |
| 113 | 2 | 111 | db 01 00 00 |
| 114 | 3 | 112 | cf 01 ff 00 c1 01 fc 00 d1 01 08 00 cf 01 00 00 c0 01 fc 00 |

### 10.3 Карта трансляции диагностических типов HHT

**type_cmd_map_V**, DS `0xcc92`, FUN_1028_0c24 @1028:0c24 (d939==0).
| Внутренний TYPE | CMD |
| --- | --- |
| 0xf0 | 0x10 |
| 0xf1 | 0x11 |
| 0xf2 | 0x12 |
| 0xf3 | 0x13 |
| 0xf4 | 0x14 |
| 0xf5 | 0x15 |
| 0xf6 | 0x16 |
| 0xf7 | 0x17 |
| 0xf8 | 0x18 |
| 0xf9 | 0x19 |
| 0xfa | 0x1a |
| 0xfb | 0x1b |
| 0xfc | 0x1c |
| 0xfd | 0x1d |
| 0xfe | 0x1e |
| 0xff | 0x1f |
| 0x80 | 0x40 |
| 0x81 | 0x41 |
| 0x82 | 0x42 |
| 0x83 | 0x43 |
| 0x84 | 0x44 |
| 0x85 | 0x45 |
| 0x86 | 0x46 |
| 0x87 | 0x47 |
| 0x88 | 0x48 |
| 0x89 | 0x49 |
| 0x8a | 0x4a |
| 0x8b | 0x4b |
| 0x8c | 0x4c |
| 0x8d | 0x4d |
| 0x8e | 0x4e |
| 0x8f | 0x4f |
| 0x90 | 0x50 |
| 0x91 | 0x51 |
| 0x92 | 0x52 |
| 0x93 | 0x53 |
| 0x94 | 0x54 |
| 0x95 | 0x55 |
| 0x96 | 0x56 |
| 0x97 | 0x57 |
| 0x98 | 0x58 |
| 0x99 | 0x59 |
| 0x9a | 0x5a |
| 0x9b | 0x5b |
| 0x9c | 0x5c |
| 0x9d | 0x5d |
| 0x9e | 0x5e |
| 0x9f | 0x5f |
| 0xa0 | 0x60 |
| 0xa1 | 0x61 |
| 0xa2 | 0x62 |
| 0xa3 | 0x63 |
| 0xa4 | 0x64 |
| 0xa5 | 0x65 |
| 0xa6 | 0x66 |
| 0xa7 | 0x67 |
| 0xa8 | 0x68 |
| 0xa9 | 0x69 |
| 0xaa | 0x6a |
| 0xab | 0x6b |
| 0xac | 0x6c |
| 0xad | 0x6d |
| 0xae | 0x6e |
| 0xaf | 0x6f |
| 0xb0 | 0x70 |
| 0xb1 | 0x71 |
| 0xb2 | 0x72 |
| 0xb3 | 0x73 |
| 0xb4 | 0x74 |
| 0xb5 | 0x75 |
| 0xb6 | 0x76 |
| 0xb7 | 0x77 |
| 0xb8 | 0x78 |
| 0xb9 | 0x79 |
| 0xba | 0x7a |
| 0xbb | 0x7b |
| 0xbc | 0x7c |
| 0xbd | 0x7d |
| 0xbe | 0x7e |
| 0xbf | 0x7f |

**type_cmd_map_B**, DS `0xcc68`, FUN_1028_0c24 @1028:0c24 (d939==1).
| Внутренний TYPE | CMD |
| --- | --- |
| 0x01 | 0x11 |
| 0x02 | 0x12 |
| 0x03 | 0x13 |
| 0x04 | 0x14 |
| 0xf0 | 0x20 |
| 0xf1 | 0x21 |
| 0xf2 | 0x22 |
| 0xf3 | 0x23 |
| 0xf4 | 0x24 |
| 0xf5 | 0x25 |
| 0xf6 | 0x26 |
| 0xf7 | 0x27 |
| 0xf8 | 0x28 |
| 0xf9 | 0x29 |
| 0xfa | 0x2a |
| 0xfb | 0x2b |
| 0xfc | 0x2c |
| 0xfd | 0x2d |
| 0xfe | 0x2e |
| 0xff | 0x2f |

### 10.4 Статические HFM-команды HHT-WIN по варианту V/B

| Индекс | Вариант | DS | AT | ER | Ответ (внутр.) | ROM desc | Flags | Связанные ID | Шаблон HHT | Timing |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | V | 0x834e | 10 | 2 | 4 | 1 | 0 | [] | 12 01 76 00 | 2 |
| 1 | B | 0x835a | 11 | 1 | 2 | 1 | 0 | [] | bf 00 | 1 |
| 2 | V | 0x8366 | 12 | 3 | 16 | 3 | 1 | [6] | 12 01 a4 00 12 01 a5 00 12 01 7e 00 12 01 7e 00 | 3 |
| 2 | B | 0x8372 | 13 | 3 | 10 | 3 | 0 | [2,6] | 21 01 d7 00 20 01 67 00 b7 00 | 3 |
| 3 | V | 0x837e | 14 | 3 | 16 | 4 | 1 | [6] | 22 01 74 00 22 01 75 00 12 01 7e 00 12 01 7e 00 | 3 |
| 3 | B | 0x838a | 15 | 3 | 10 | 4 | 1 | [6] | 20 01 54 00 20 01 55 00 b7 00 | 3 |
| 4 | V | 0x8396 | 16 | 3 | 16 | 5 | 1 | [6] | 22 01 7f 00 22 01 7f 00 12 01 7e 00 12 01 7e 00 | 3 |
| 4 | B | 0x83a2 | 17 | 3 | 4 | 5 | 0 | [6] | b9 00 b7 00 | 3 |
| 5 | V | 0x83ae | 18 | 2 | 4 | 6 | 0 | [] | 22 01 78 00 | 2 |
| 5 | B | 0x83ba | 19 | 1 | 2 | 6 | 0 | [] | bd 00 | 1 |
| 6 | V | 0x83c6 | 20 | 3 | 8 | 2 | 1 | [] | 12 01 7e 00 12 01 7e 00 | 3 |
| 6 | B | 0x83d2 | 21 | 1 | 2 | 2 | 0 | [] | b7 00 | 1 |
| 7 | V | 0x83de | 22 | 3 | 16 | 7 | 1 | [6] | 22 01 72 00 22 01 73 00 12 01 7e 00 12 01 7e 00 | 3 |
| 7 | B | 0x83ea | 23 | 3 | 10 | 7 | 0 | [7,6] | 20 01 48 00 20 01 49 00 b7 00 | 3 |
| 8 | V | 0x83f6 | 24 | 3 | 16 | 8 | 1 | [6] | 20 01 f2 00 20 01 f2 00 12 01 7e 00 12 01 7e 00 | 3 |
| 8 | B | 0x8402 | 25 | 3 | 6 | 8 | 0 | [6] | 20 01 23 00 b7 00 | 3 |
| 9 | V | 0x840e | 26 | 3 | 8 | 0 | 0 | [] | 20 01 43 00 20 01 42 00 | 3 |
| 10 | V | 0x8426 | 28 | 3 | 8 | 10 | 1 | [] | 20 01 45 00 20 01 45 00 | 3 |
| 10 | B | 0x8432 | 29 | 2 | 4 | 10 | 0 | [] | 24 01 92 00 | 2 |
| 11 | V | 0x843e | 30 | 3 | 8 | 11 | 1 | [] | 20 01 47 00 20 01 47 00 | 3 |
| 11 | B | 0x844a | 31 | 2 | 4 | 11 | 0 | [] | 24 01 93 00 | 2 |
| 12 | V | 0x8456 | 32 | 3 | 16 | 12 | 1 | [6] | 12 01 72 00 12 01 73 00 12 01 7e 00 12 01 7e 00 | 3 |
| 12 | B | 0x8462 | 33 | 3 | 4 | 12 | 0 | [6] | c4 00 b7 00 | 3 |
| 13 | V | 0x846e | 50 | 2 | 4 | 13 | 0 | [] | 12 01 77 00 | 2 |
| 13 | B | 0x847a | 51 | 1 | 2 | 13 | 0 | [] | be 00 | 1 |
| 14 | V | 0x8486 | 34 | 3 | 10 | 14 | 2 | [] | 00 01 7e 00 90 01 34 00 80 00 | 3 |
| 14 | B | 0x8492 | 35 | 2 | 4 | 14 | 0 | [] | 20 01 39 00 | 2 |
| 15 | V | 0x849e | 36 | 2 | 4 | 24 | 0 | [] | 23 01 a4 00 | 2 |
| 15 | B | 0x84aa | 37 | 1 | 2 | 24 | 0 | [] | 98 00 | 1 |
| 16 | V | 0x84b6 | 38 | 2 | 4 | 26 | 0 | [] | 23 01 7f 00 | 2 |
| 16 | B | 0x84c2 | 39 | 1 | 2 | 26 | 0 | [] | 69 00 | 1 |
| 17 | V | 0x84ce | 40 | 2 | 4 | 26 | 0 | [] | 23 01 7e 00 | 2 |
| 17 | B | 0x84da | 41 | 1 | 2 | 26 | 0 | [] | 69 00 | 1 |
| 18 | V | 0x84e6 | 42 | 2 | 4 | 25 | 0 | [] | 12 01 7b 00 | 2 |
| 18 | B | 0x84f2 | 43 | 1 | 2 | 25 | 0 | [] | 67 00 | 1 |
| 19 | V | 0x84fe | 44 | 2 | 4 | 25 | 0 | [] | 22 01 7b 00 | 2 |
| 19 | B | 0x850a | 45 | 1 | 2 | 25 | 0 | [] | 67 00 | 1 |
| 20 | V | 0x8516 | 46 | 2 | 4 | 24 | 0 | [] | 12 01 7a 00 | 2 |
| 20 | B | 0x8522 | 47 | 1 | 2 | 24 | 0 | [] | 98 00 | 1 |
| 21 | V | 0x852e | 48 | 2 | 4 | 24 | 0 | [] | 12 01 7a 00 | 2 |
| 21 | B | 0x853a | 49 | 1 | 2 | 24 | 0 | [] | 99 00 | 1 |
| 22 | V | 0x8546 | 62 | 2 | 4 | 24 | 0 | [] | 22 01 7a 00 | 2 |
| 22 | B | 0x8552 | 63 | 1 | 2 | 24 | 0 | [] | 98 00 | 1 |
| 23 | V | 0x855e | 52 | 2 | 4 | 26 | 0 | [] | 22 01 7a 00 | 2 |
| 23 | B | 0x856a | 53 | 1 | 2 | 26 | 0 | [] | 69 00 | 1 |
| 24 | V | 0x8576 | 54 | 2 | 4 | 25 | 0 | [] | 22 01 7a 00 | 2 |
| 24 | B | 0x8582 | 55 | 1 | 2 | 25 | 0 | [] | 67 00 | 1 |
| 25 | V | 0x858e | 56 | 2 | 4 | 25 | 0 | [] | 22 01 7a 00 | 2 |
| 25 | B | 0x859a | 57 | 1 | 2 | 25 | 0 | [] | 67 00 | 1 |
| 26 | V | 0x85a6 | 58 | 2 | 4 | 26 | 0 | [] | 22 01 7a 00 | 2 |
| 26 | B | 0x85b2 | 59 | 1 | 2 | 26 | 0 | [] | 68 00 | 1 |
| 27 | V | 0x85be | 60 | 3 | 8 | 15 | 1 | [] | 20 01 fc 00 20 01 fc 00 | 3 |
| 27 | B | 0x85ca | 61 | 2 | 4 | 15 | 0 | [] | 20 01 41 00 | 2 |
| 28 | V | 0x85d6 | 12 | 3 | 16 | 3 | 1 | [6] | 12 01 a4 00 12 01 a5 00 12 01 7e 00 12 01 7e 00 | 3 |
| 28 | B | 0x85e2 | 13 | 3 | 10 | 3 | 0 | [2,6] | 21 01 d7 00 20 01 67 00 b7 00 | 3 |
| 29 | V | 0x85ee | 26 | 3 | 8 | 9 | 1 | [] | 20 01 43 00 20 01 42 00 | 3 |
| 29 | B | 0x85fa | 27 | 2 | 4 | 9 | 0 | [] | 24 01 90 00 | 2 |
| 31 | V | 0x861e | 20 | 3 | 8 | 2 | 1 | [] | 12 01 7e 00 12 01 7e 00 | 3 |
| 31 | B | 0x862a | 21 | 1 | 2 | 2 | 0 | [] | b7 00 | 1 |
| 39 | V | 0x86de | 18 | 2 | 4 | 6 | 0 | [] | 22 01 78 00 | 2 |
| 39 | B | 0x86ea | 19 | 1 | 2 | 6 | 0 | [] | bd 00 | 1 |
| 42 | V | 0x8726 | 88 | 3 | 20 | 0 | 0 | [] | 10 01 02 00 22 01 22 00 10 01 03 00 22 01 23 00 10 01 04 00 | 3 |
| 42 | B | 0x8732 | 89 | 3 | 8 | 0 | 0 | [] | 27 01 02 00 24 01 f0 00 | 3 |
| 43 | V | 0x873e | 91 | 2 | 4 | 16 | 0 | [] | 12 01 7f 00 | 2 |
| 43 | B | 0x874a | 92 | 2 | 4 | 16 | 0 | [] | 20 01 47 00 | 2 |
| 44 | V | 0x8756 | 24 | 3 | 16 | 8 | 1 | [6] | 20 01 f2 00 20 01 f2 00 12 01 7e 00 12 01 7e 00 | 3 |
| 44 | B | 0x8762 | 25 | 3 | 6 | 8 | 0 | [6] | 20 01 23 00 b7 00 | 3 |
| 45 | V | 0x876e | 10 | 2 | 4 | 1 | 0 | [] | 12 01 76 00 | 2 |
| 45 | B | 0x877a | 11 | 1 | 2 | 1 | 0 | [] | bf 00 | 1 |
| 46 | B | 0x8792 | 98 | 3 | 6 | 20 | 0 | [6] | 20 01 25 00 b7 00 | 3 |
| 47 | V | 0x879e | 94 | 0 | 0 | 21 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 47 | B | 0x87aa | 94 | 0 | 0 | 21 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 48 | V | 0x87b6 | 94 | 0 | 0 | 22 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 48 | B | 0x87c2 | 94 | 0 | 0 | 22 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 49 | V | 0x87ce | 94 | 0 | 0 | 23 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 49 | B | 0x87da | 94 | 0 | 0 | 23 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 50 | V | 0x87e6 | 26 | 3 | 8 | 9 | 1 | [] | 20 01 43 00 20 01 42 00 | 3 |
| 50 | B | 0x87f2 | 27 | 2 | 4 | 9 | 0 | [] | 24 01 90 00 | 2 |
| 51 | V | 0x87fe | 28 | 3 | 8 | 10 | 1 | [] | 20 01 45 00 20 01 45 00 | 3 |
| 51 | B | 0x880a | 29 | 2 | 4 | 10 | 0 | [] | 24 01 92 00 | 2 |
| 52 | V | 0x8816 | 30 | 3 | 8 | 11 | 1 | [] | 20 01 47 00 20 01 47 00 | 3 |
| 52 | B | 0x8822 | 31 | 2 | 4 | 11 | 0 | [] | 24 01 93 00 | 2 |
| 53 | V | 0x882e | 94 | 0 | 0 | 26 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 53 | B | 0x883a | 94 | 0 | 0 | 26 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 54 | V | 0x8846 | 94 | 0 | 0 | 26 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 54 | B | 0x8852 | 94 | 0 | 0 | 26 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 55 | V | 0x885e | 94 | 0 | 0 | 27 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 55 | B | 0x886a | 94 | 0 | 0 | 27 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 56 | V | 0x8876 | 94 | 0 | 0 | 38 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 56 | B | 0x8882 | 94 | 0 | 0 | 38 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 57 | V | 0x888e | 94 | 0 | 0 | 39 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 57 | B | 0x889a | 94 | 0 | 0 | 39 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 58 | V | 0x88a6 | 94 | 0 | 0 | 40 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 58 | B | 0x88b2 | 94 | 0 | 0 | 40 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 59 | V | 0x88be | 94 | 0 | 0 | 19 | 1 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 59 | B | 0x88ca | 94 | 0 | 0 | 19 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 60 | V | 0x88d6 | 94 | 0 | 0 | 42 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 60 | B | 0x88e2 | 94 | 0 | 0 | 42 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 61 | V | 0x88ee | 94 | 0 | 0 | 43 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 61 | B | 0x88fa | 94 | 0 | 0 | 43 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 62 | V | 0x8906 | 10 | 2 | 4 | 44 | 0 | [] | 12 01 76 00 | 2 |
| 62 | B | 0x8912 | 11 | 1 | 2 | 44 | 0 | [] | bf 00 | 1 |
| 63 | V | 0x891e | 94 | 0 | 0 | 45 | 1 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 63 | B | 0x892a | 94 | 0 | 0 | 45 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 64 | V | 0x8936 | 34 | 3 | 10 | 14 | 2 | [] | 00 01 7e 00 90 01 34 00 80 00 | 3 |
| 64 | B | 0x8942 | 35 | 2 | 4 | 14 | 0 | [] | 20 01 39 00 | 2 |
| 65 | V | 0x894e | 94 | 0 | 0 | 54 | 0 | [67,68,69,70,71] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 65 | B | 0x895a | 94 | 0 | 0 | 54 | 0 | [67,68,69,70,71] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 66 | V | 0x8966 | 94 | 0 | 0 | 54 | 0 | [67,68,69] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 66 | B | 0x8972 | 94 | 0 | 0 | 54 | 0 | [67,68,69] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 67 | V | 0x897e | 0 | 0 | 0 | 55 | 0 | [] | 00 00 aa 00 | 0 |
| 67 | B | 0x898a | 0 | 0 | 0 | 55 | 0 | [] | 00 00 aa 00 | 0 |
| 68 | V | 0x8996 | 0 | 0 | 0 | 56 | 0 | [] | 00 00 aa 00 | 0 |
| 68 | B | 0x89a2 | 0 | 0 | 0 | 56 | 0 | [] | 00 00 aa 00 | 0 |
| 69 | V | 0x89ae | 0 | 0 | 0 | 57 | 0 | [] | 00 00 aa 00 | 0 |
| 69 | B | 0x89ba | 0 | 0 | 0 | 57 | 0 | [] | 00 00 aa 00 | 0 |
| 70 | V | 0x89c6 | 0 | 0 | 0 | 58 | 0 | [] | 00 00 aa 00 | 0 |
| 70 | B | 0x89d2 | 0 | 0 | 0 | 58 | 0 | [] | 00 00 aa 00 | 0 |
| 71 | V | 0x89de | 0 | 0 | 0 | 59 | 0 | [] | 00 00 aa 00 | 0 |
| 71 | B | 0x89ea | 0 | 0 | 0 | 59 | 0 | [] | 00 00 aa 00 | 0 |
| 72 | V | 0x89f6 | 94 | 0 | 0 | 61 | 0 | [75,73] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 72 | B | 0x8a02 | 94 | 0 | 0 | 61 | 0 | [75,73] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 73 | V | 0x8a0e | 94 | 0 | 0 | 60 | 0 | [74] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 73 | B | 0x8a1a | 94 | 0 | 0 | 60 | 0 | [74] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 74 | V | 0x8a26 | 0 | 0 | 0 | 61 | 0 | [] | 00 00 aa 00 | 0 |
| 74 | B | 0x8a32 | 0 | 0 | 0 | 61 | 0 | [] | 00 00 aa 00 | 0 |
| 75 | V | 0x8a3e | 0 | 0 | 0 | 62 | 0 | [] | 00 00 aa 00 | 0 |
| 75 | B | 0x8a4a | 0 | 0 | 0 | 62 | 0 | [] | 00 00 aa 00 | 0 |
| 76 | V | 0x8a56 | 94 | 0 | 0 | 64 | 1 | [79,77,6] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 76 | B | 0x8a62 | 94 | 0 | 0 | 64 | 0 | [79,77,6] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 77 | V | 0x8a6e | 94 | 0 | 0 | 63 | 1 | [78,6] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 77 | B | 0x8a7a | 94 | 0 | 0 | 63 | 0 | [78,6] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 78 | V | 0x8a86 | 0 | 0 | 0 | 64 | 1 | [] | 00 00 aa 00 | 0 |
| 78 | B | 0x8a92 | 0 | 0 | 0 | 64 | 0 | [] | 00 00 aa 00 | 0 |
| 79 | V | 0x8a9e | 0 | 0 | 0 | 65 | 1 | [] | 00 00 aa 00 | 0 |
| 79 | B | 0x8aaa | 0 | 0 | 0 | 65 | 0 | [] | 00 00 aa 00 | 0 |
| 80 | V | 0x8ab6 | 94 | 0 | 0 | 67 | 0 | [83,81,6] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 80 | B | 0x8ac2 | 94 | 0 | 0 | 67 | 0 | [83,81,6] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 81 | V | 0x8ace | 94 | 0 | 0 | 66 | 0 | [82,6] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 81 | B | 0x8ada | 94 | 0 | 0 | 66 | 0 | [82,6] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 82 | V | 0x8ae6 | 0 | 0 | 0 | 67 | 0 | [] | 00 00 aa 00 | 0 |
| 82 | B | 0x8af2 | 0 | 0 | 0 | 67 | 0 | [] | 00 00 aa 00 | 0 |
| 83 | V | 0x8afe | 0 | 0 | 0 | 68 | 0 | [] | 00 00 aa 00 | 0 |
| 83 | B | 0x8b0a | 0 | 0 | 0 | 68 | 0 | [] | 00 00 aa 00 | 0 |
| 84 | V | 0x8b16 | 94 | 0 | 0 | 69 | 0 | [86,87,88,89,90,115] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 84 | B | 0x8b22 | 94 | 0 | 0 | 69 | 0 | [86,87,88,89,90,115] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 85 | V | 0x8b2e | 94 | 0 | 0 | 69 | 0 | [86,87,88,115] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 85 | B | 0x8b3a | 94 | 0 | 0 | 69 | 0 | [86,87,88,115] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 86 | V | 0x8b46 | 0 | 0 | 0 | 70 | 0 | [] | 00 00 aa 00 | 0 |
| 86 | B | 0x8b52 | 0 | 0 | 0 | 70 | 0 | [] | 00 00 aa 00 | 0 |
| 87 | V | 0x8b5e | 0 | 0 | 0 | 71 | 0 | [] | 00 00 aa 00 | 0 |
| 87 | B | 0x8b6a | 0 | 0 | 0 | 71 | 0 | [] | 00 00 aa 00 | 0 |
| 88 | V | 0x8b76 | 0 | 0 | 0 | 72 | 0 | [] | 00 00 aa 00 | 0 |
| 88 | B | 0x8b82 | 0 | 0 | 0 | 72 | 0 | [] | 00 00 aa 00 | 0 |
| 89 | V | 0x8b8e | 0 | 0 | 0 | 73 | 0 | [] | 00 00 aa 00 | 0 |
| 89 | B | 0x8b9a | 0 | 0 | 0 | 73 | 0 | [] | 00 00 aa 00 | 0 |
| 90 | V | 0x8ba6 | 0 | 0 | 0 | 74 | 0 | [] | 00 00 aa 00 | 0 |
| 90 | B | 0x8bb2 | 0 | 0 | 0 | 74 | 0 | [] | 00 00 aa 00 | 0 |
| 91 | V | 0x8bbe | 94 | 0 | 0 | 46 | 1 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 91 | B | 0x8bca | 94 | 0 | 0 | 46 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 92 | V | 0x8bd6 | 94 | 0 | 0 | 17 | 1 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 92 | B | 0x8be2 | 94 | 0 | 0 | 17 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 93 | V | 0x8bee | 94 | 0 | 0 | 47 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 93 | B | 0x8bfa | 94 | 0 | 0 | 47 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 94 | V | 0x8c06 | 94 | 0 | 0 | 75 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 94 | B | 0x8c12 | 94 | 0 | 0 | 75 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 95 | V | 0x8c1e | 94 | 0 | 0 | 18 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 95 | B | 0x8c2a | 94 | 0 | 0 | 18 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 96 | V | 0x8c36 | 94 | 0 | 0 | 48 | 1 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 96 | B | 0x8c42 | 94 | 0 | 0 | 48 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 97 | V | 0x8c4e | 94 | 0 | 0 | 49 | 1 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 97 | B | 0x8c5a | 94 | 0 | 0 | 49 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 98 | B | 0x8c72 | 94 | 0 | 0 | 50 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 99 | V | 0x8c7e | 94 | 0 | 0 | 51 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 99 | B | 0x8c8a | 94 | 0 | 0 | 51 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 100 | V | 0x8c96 | 94 | 0 | 0 | 76 | 1 | [101,102,103] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 100 | B | 0x8ca2 | 94 | 0 | 0 | 76 | 0 | [101,102,103] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 101 | V | 0x8cae | 0 | 0 | 0 | 77 | 1 | [] | 00 00 aa 00 | 0 |
| 101 | B | 0x8cba | 0 | 0 | 0 | 77 | 0 | [] | 00 00 aa 00 | 0 |
| 102 | V | 0x8cc6 | 0 | 0 | 0 | 78 | 1 | [] | 00 00 aa 00 | 0 |
| 102 | B | 0x8cd2 | 0 | 0 | 0 | 78 | 0 | [] | 00 00 aa 00 | 0 |
| 103 | V | 0x8cde | 0 | 0 | 0 | 79 | 0 | [] | 00 00 aa 00 | 0 |
| 103 | B | 0x8cea | 0 | 0 | 0 | 79 | 0 | [] | 00 00 aa 00 | 0 |
| 104 | B | 0x8d02 | 94 | 0 | 0 | 86 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 105 | V | 0x8d0e | 94 | 0 | 0 | 82 | 0 | [108,109] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 106 | B | 0x8d32 | 94 | 0 | 0 | 81 | 0 | [107,108] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 107 | B | 0x8d4a | 0 | 0 | 0 | 82 | 0 | [] | 00 00 aa 00 | 0 |
| 108 | V | 0x8d56 | 0 | 0 | 0 | 83 | 0 | [] | 00 00 aa 00 | 0 |
| 108 | B | 0x8d62 | 0 | 0 | 0 | 83 | 1 | [] | 00 00 aa 00 | 0 |
| 109 | V | 0x8d6e | 0 | 0 | 0 | 84 | 0 | [] | 00 00 aa 00 | 0 |
| 110 | V | 0x8d86 | 94 | 0 | 0 | 41 | 1 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 110 | B | 0x8d92 | 94 | 0 | 0 | 41 | 1 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 111 | V | 0x8d9e | 36 | 2 | 4 | 80 | 0 | [] | 23 01 a4 00 | 2 |
| 111 | B | 0x8daa | 94 | 0 | 0 | 80 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 112 | V | 0x8db6 | 36 | 2 | 4 | 80 | 0 | [] | 23 01 a4 00 | 2 |
| 112 | B | 0x8dc2 | 94 | 0 | 0 | 80 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 113 | V | 0x8dce | 36 | 2 | 4 | 24 | 0 | [] | 23 01 a4 00 | 2 |
| 113 | B | 0x8dda | 94 | 0 | 0 | 24 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 114 | V | 0x8de6 | 36 | 2 | 4 | 24 | 0 | [] | 23 01 a4 00 | 2 |
| 114 | B | 0x8df2 | 94 | 0 | 0 | 24 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 115 | V | 0x8dfe | 36 | 2 | 4 | 80 | 0 | [] | 23 01 a4 00 | 2 |
| 115 | B | 0x8e0a | 94 | 0 | 0 | 80 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 116 | V | 0x8e16 | 36 | 2 | 4 | 80 | 0 | [] | 23 01 a4 00 | 2 |
| 116 | B | 0x8e22 | 94 | 0 | 0 | 80 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 117 | V | 0x8e2e | 36 | 2 | 4 | 80 | 0 | [] | 23 01 a4 00 | 2 |
| 117 | B | 0x8e3a | 94 | 0 | 0 | 80 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 118 | V | 0x8e46 | 36 | 2 | 4 | 80 | 0 | [] | 23 01 a4 00 | 2 |
| 118 | B | 0x8e52 | 94 | 0 | 0 | 80 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 119 | V | 0x8e5e | 36 | 2 | 4 | 80 | 0 | [] | 23 01 a4 00 | 2 |
| 119 | B | 0x8e6a | 94 | 0 | 0 | 80 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 120 | V | 0x8e76 | 36 | 2 | 4 | 80 | 0 | [] | 23 01 a4 00 | 2 |
| 120 | B | 0x8e82 | 94 | 0 | 0 | 80 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 121 | V | 0x8e8e | 94 | 0 | 0 | 53 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 121 | B | 0x8e9a | 94 | 0 | 0 | 53 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 122 | V | 0x8ea6 | 94 | 0 | 0 | 82 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 123 | B | 0x8eca | 94 | 0 | 0 | 81 | 0 | [107] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 124 | V | 0x8ed6 | 94 | 0 | 0 | 90 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 124 | B | 0x8ee2 | 94 | 0 | 0 | 90 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 125 | V | 0x8eee | 94 | 0 | 0 | 91 | 2 | [127,128,129,130,131] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 125 | B | 0x8efa | 94 | 0 | 0 | 91 | 2 | [127,128,129,130,131] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 126 | V | 0x8f06 | 94 | 0 | 0 | 91 | 2 | [127,128,129] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 126 | B | 0x8f12 | 94 | 0 | 0 | 91 | 2 | [127,128,129] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 127 | V | 0x8f1e | 0 | 0 | 0 | 92 | 2 | [] | 00 00 aa 00 | 0 |
| 127 | B | 0x8f2a | 0 | 0 | 0 | 92 | 2 | [] | 00 00 aa 00 | 0 |
| 128 | V | 0x8f36 | 0 | 0 | 0 | 93 | 2 | [] | 00 00 aa 00 | 0 |
| 128 | B | 0x8f42 | 0 | 0 | 0 | 93 | 2 | [] | 00 00 aa 00 | 0 |
| 129 | V | 0x8f4e | 0 | 0 | 0 | 94 | 2 | [] | 00 00 aa 00 | 0 |
| 129 | B | 0x8f5a | 0 | 0 | 0 | 94 | 2 | [] | 00 00 aa 00 | 0 |
| 130 | V | 0x8f66 | 0 | 0 | 0 | 95 | 2 | [] | 00 00 aa 00 | 0 |
| 130 | B | 0x8f72 | 0 | 0 | 0 | 95 | 2 | [] | 00 00 aa 00 | 0 |
| 131 | V | 0x8f7e | 0 | 0 | 0 | 96 | 2 | [] | 00 00 aa 00 | 0 |
| 131 | B | 0x8f8a | 0 | 0 | 0 | 96 | 2 | [] | 00 00 aa 00 | 0 |
| 132 | V | 0x8f96 | 94 | 0 | 0 | 30 | 0 | [133,134,135,136] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 132 | B | 0x8fa2 | 94 | 0 | 0 | 30 | 0 | [133,134,135,136] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 133 | V | 0x8fae | 94 | 0 | 0 | 31 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 133 | B | 0x8fba | 94 | 0 | 0 | 31 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 134 | V | 0x8fc6 | 94 | 0 | 0 | 32 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 134 | B | 0x8fd2 | 94 | 0 | 0 | 32 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 135 | V | 0x8fde | 94 | 0 | 0 | 33 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 135 | B | 0x8fea | 94 | 0 | 0 | 33 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 136 | V | 0x8ff6 | 94 | 0 | 0 | 34 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 136 | B | 0x9002 | 94 | 0 | 0 | 34 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 137 | V | 0x900e | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 137 | B | 0x901a | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 138 | V | 0x9026 | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 138 | B | 0x9032 | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 139 | V | 0x903e | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 139 | B | 0x904a | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 140 | V | 0x9056 | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 140 | B | 0x9062 | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 141 | V | 0x906e | 94 | 0 | 0 | 26 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 141 | B | 0x907a | 94 | 0 | 0 | 26 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 142 | V | 0x9086 | 94 | 0 | 0 | 24 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 142 | B | 0x9092 | 94 | 0 | 0 | 24 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 143 | V | 0x909e | 94 | 0 | 0 | 35 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 143 | B | 0x90aa | 94 | 0 | 0 | 35 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 144 | V | 0x90b6 | 94 | 0 | 0 | 35 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 144 | B | 0x90c2 | 94 | 0 | 0 | 35 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 145 | V | 0x90ce | 94 | 0 | 0 | 14 | 2 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 146 | V | 0x90e6 | 94 | 0 | 0 | 54 | 0 | [147] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 146 | B | 0x90f2 | 94 | 0 | 0 | 54 | 0 | [147] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 147 | V | 0x90fe | 0 | 0 | 0 | 55 | 0 | [] | 00 00 aa 00 | 0 |
| 147 | B | 0x910a | 0 | 0 | 0 | 55 | 0 | [] | 00 00 aa 00 | 0 |
| 148 | V | 0x9116 | 94 | 0 | 0 | 53 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 148 | B | 0x9122 | 94 | 0 | 0 | 53 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 149 | V | 0x912e | 94 | 0 | 0 | 25 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 149 | B | 0x913a | 94 | 0 | 0 | 25 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 150 | V | 0x9146 | 94 | 0 | 0 | 28 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 152 | V | 0x9176 | 94 | 0 | 0 | 97 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 152 | B | 0x9182 | 94 | 0 | 0 | 97 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 153 | V | 0x918e | 91 | 2 | 4 | 29 | 0 | [] | 12 01 7f 00 | 2 |
| 155 | V | 0x91be | 94 | 0 | 0 | 14 | 0 | [156] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 156 | V | 0x91d6 | 94 | 0 | 0 | 15 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 157 | V | 0x91ee | 94 | 0 | 0 | 19 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 158 | V | 0x9206 | 94 | 0 | 0 | 20 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 159 | V | 0x921e | 94 | 0 | 0 | 65 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 160 | V | 0x9236 | 94 | 0 | 0 | 66 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 161 | V | 0x924e | 94 | 0 | 0 | 67 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 162 | V | 0x9266 | 94 | 0 | 0 | 68 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 163 | V | 0x927e | 94 | 0 | 0 | 73 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 164 | V | 0x9296 | 94 | 0 | 0 | 74 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 165 | V | 0x92ae | 94 | 0 | 0 | 25 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 168 | V | 0x92f6 | 20 | 3 | 8 | 2 | 1 | [] | 12 01 7e 00 12 01 7e 00 | 3 |
| 168 | B | 0x9302 | 21 | 1 | 2 | 2 | 0 | [] | b7 00 | 1 |
| 170 | V | 0x9326 | 94 | 0 | 0 | 36 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 170 | B | 0x9332 | 94 | 0 | 0 | 36 | 0 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 171 | V | 0x933e | 94 | 0 | 0 | 75 | 2 | [] | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 3 |
| 172 | V | 0x9356 | 91 | 2 | 4 | 16 | 0 | [] | 12 01 7f 00 | 2 |
| 172 | B | 0x9362 | 92 | 2 | 4 | 16 | 0 | [] | 20 01 47 00 | 2 |
| 173 | B | 0x937a | 0 | 0 | 36 | 0 | 0 | [] | 00 00 aa 00 | 0 |
| 174 | V | 0x9386 | 771 | 0 | 0 | 0 | 0 | [] | — | — |
| 174 | B | 0x9392 | 1 | 0 | 3 | 1 | 0 | [] | 10 01 aa 00 | 4 |
| 175 | V | 0x939e | 37 | 5 | 5 | 0 | 0 | [] | 98 00 | 1 |
| 175 | B | 0x93aa | 0 | 0 | 37 | 0 | 49 | [] | 00 00 aa 00 | 0 |

Разделение V/B вычисляется по флагу/диагностическому идентификатору (`d939`). Поля `AT`, `ER`, `ROM desc` — внутренние индексы HHT; в наш драйвер они должны быть преобразованы в явные алгоритмы. Некоторые последовательности содержат одинарный селектор, работающий в ранее установленном контексте.
## 11. Заводские диагностические экраны HHT-WIN (все 173 записи)

`istwert_records.json` содержит **173 записи элементов диагностических экранов**, включая Live Data, статусы, заголовки, тесты и элементы выбора. НЕ интерпретировать 173 как число уникальных измеряемых величин. Колонка `conv` — сегмент:смещение функции пересчёта; `list_items` — внутренние шаблоны экранов, не самостоятельные K-Line пакеты.
| № | DS | Наименование в HHT | Конвертер | List-поле | Сырой шаблон/список | Аргументы | Текст. индексы |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 0x6d16 | 01 Coolant temperature | 8:0000 | 9:6aa4 | ["01 15 01 00 bd 01"] | [1,1] | 564/585 |
| 2 | 0x6d36 | 02 Engine speed | 8:0042 | 9:6aaa | ["01 04 01 00 bd 01","01 11 01 00 bd 01"] | [1,2] | 565/585 |
| 3 | 0x6d56 | 03 Injection time | 8:0120 | 9:6ab6 | ["01 14 01 00 be 01"] | [1,3] | 0/585 |
| 4 | 0x6d76 | 04 Lambda control | 8:0292 | 9:6abc | ["01 16 01 00 bd 01"] | [1,4] | 0/585 |
| 5 | 0x6d96 | 05 Throttle valve angle | 8:0361 | 9:6ac2 | ["01 15 01 00 be 01"] | [1,5] | 0/585 |
| 6 | 0x6db6 | 02 Engine speed | 8:03c8 | 9:6ac8 | ["01 11 01 00 bd 01"] | [1,6] | 0/585 |
| 7 | 0x6dd6 | 06 Air mass | 8:042f | 9:6ace | ["01 12 01 00 bd 01"] | [1,7] | 564/585 |
| 8 | 0x6df6 | 07 Oxygen sensor voltage | 8:04df | 9:6ad4 | ["01 14 01 00 bd 01"] | [1,8] | 0/585 |
| 9 | 0x6e16 | 08 Self-adjustment | 8:0633 | 9:6ada | ["01 15 01 00 bd 01"] | [1,9] | 0/585 |
| 10 | 0x6e36 | 09 Self-adjustment factor | 8:0777 | 9:6afe | ["01 17 01 00 bf 01"] | [1,10] | 0/585 |
| 11 | 0x6e56 | 10 Self-adjustment factor | 8:085e | 9:6afe | ["01 17 01 00 bf 01"] | [1,11] | 0/585 |
| 12 | 0x6e76 | 11 Ignition angle | 8:0945 | 9:6ae0 | ["01 14 01 00 bd 01"] | [1,12] | 0/585 |
| 13 | 0x6e96 | 12 Intake air temperature | 8:0a87 | 9:6aa4 | ["01 15 01 00 bd 01"] | [1,13] | 564/585 |
| 14 | 0x6eb6 | 13 Res. trimming plug | 8:0ac9 | 9:6ae6 | ["06 0a 01 00 ba 01"] | [1,14] | 0/585 |
| 15 | 0x6ed6 | 14 Starter signal | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,15] | 0/585 |
| 16 | 0x6ef6 | 15 Post-start enrichment | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,16] | 0/585 |
| 17 | 0x6f16 | 16 Warm-up | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,17] | 0/585 |
| 18 | 0x6f36 | 17 Partial intake manifold | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,18] | 0/585 |
| 19 | 0x6f56 | 18 Air pump | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,19] | 0/585 |
| 20 | 0x6f76 | 19 Selector lever position | 8:0cdd | 9:6cf0 | ["06 12 01 00 68 01"] | [1,20] | 0/585 |
| 21 | 0x6f96 | 20 A/C compressor | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,21] | 0/585 |
| 22 | 0x6fb6 | 21 Idle speed contact | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,22] | 566/585 |
| 23 | 0x6fd6 | 22 Full-load contact | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,23] | 567/585 |
| 24 | 0x6ff6 | 23 Camshaft actuator | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,24] | 568/585 |
| 25 | 0x7016 | 24 Resonance intake | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,25] | 568/585 |
| 26 | 0x7036 | 25 Deceleration cut-off | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,26] | 0/585 |
| 27 | 0x7056 | 26 CO potentiometer | 8:0e46 | 9:6af2 | ["01 14 01 00 bf 01"] | [1,27] | 0/585 |
| 28 | 0x7076 | 02 Engine speed | 8:0042 | 9:6aaa | ["01 04 01 00 bd 01","01 11 01 00 bd 01"] | [1,28] | 565/585 |
| 29 | 0x7096 | 08 Self-adjustment | 8:0633 | 9:6af8 | ["01 12 01 00 be 01"] | [1,29] | 0/585 |
| 30 | 0x70b6 |  | — | — |  | — | 0/0 |
| 31 | 0x70d6 | Engine speed | 8:03c8 | 9:6b04 | ["01 11 01 00 bd 01"] | — | 0/0 |
| 32 | 0x70f6 | Cylinder: 1  3 | — | 9:6b0a | ["00 00 00 00 00 00"] | — | 0/0 |
| 33 | 0x7116 | Cylinder: 1  4 | — | 9:6b0a | ["00 00 00 00 00 00"] | — | 0/0 |
| 34 | 0x7136 | Injection valve | — | 9:6b10 | ["01 0a 01 00 bd 01","06 14 01 00 10 00"] | — | 0/0 |
| 35 | 0x7156 | Camshaft | — | 9:6b16 | ["06 14 01 00 10 00"] | — | 0/0 |
| 36 | 0x7176 | Exh. gas recirculation | — | 9:6b16 | ["06 14 01 00 10 00"] | — | 0/0 |
| 37 | 0x7196 | Intake manifold pre- | — | 9:6b16 | ["06 14 01 00 10 00"] | — | 0/0 |
| 38 | 0x71b6 | Air pump | — | 9:6b16 | ["06 14 01 00 10 00"] | — | 0/0 |
| 39 | 0x71d6 | Throttle valve angle | 8:0361 | 9:6ac2 | ["01 15 01 00 be 01"] | — | 0/0 |
| 40 | 0x71f6 | Regeneration | — | 9:6b16 | ["06 14 01 00 10 00"] | — | 0/0 |
| 41 | 0x7216 |  | — | 9:6b16 | ["06 14 01 00 10 00"] | — | 0/0 |
| 42 | 0x7236 | Transm. type: ░▒▒▒▒▒▒▒▒▒▒▒ | 8:0f5a | 9:6b22 | ["06 0f 00 00 74 00","06 16 01 00 75 00","06 0f 02 00 bb 01","06 12 03 00 76 00","06 14 04 00 73 00","06 14 05 00 73 00","06 14 07 00 73 00"] | — | 0/0 |
| 43 | 0x7256 | 27 Rear vehicle speed | 8:11fe | 9:6b4c | ["01 13 01 00 bd 01"] | [1,43] | 0/585 |
| 44 | 0x7276 | Oxygen sensor voltage | 8:04df | 9:6ad4 | ["01 14 01 00 bd 01"] | — | 0/0 |
| 45 | 0x7296 | Coolant temperature | 8:0000 | 9:6aa4 | ["01 15 01 00 bd 01"] | — | 0/0 |
| 46 | 0x72b6 | 28 Oxygen sensor voltage | 8:04df | 9:6ad4 | ["01 14 01 00 bd 01"] | [1,46] | 0/585 |
| 47 | 0x72d6 |  | — | — |  | — | 0/0 |
| 48 | 0x72f6 |  | — | — |  | — | 0/0 |
| 49 | 0x7316 |  | — | — |  | — | 0/0 |
| 50 | 0x7336 | Self-adjustment | 8:0633 | 9:6af8 | ["01 12 01 00 be 01"] | — | 0/0 |
| 51 | 0x7356 | Self-adjustment factor | 8:0777 | 9:6afe | ["01 17 01 00 bf 01"] | — | 0/0 |
| 52 | 0x7376 | Self-adjustment factor | 8:085e | 9:6afe | ["01 17 01 00 bf 01"] | — | 0/0 |
| 53 | 0x7396 | Throttle valve stop | 8:0cdd | 9:6b1c | ["06 14 01 00 73 00"] | — | 0/0 |
| 54 | 0x73b6 |  | — | — |  | — | 0/0 |
| 55 | 0x73d6 |  | — | — |  | — | 0/0 |
| 56 | 0x73f6 |  | — | — |  | — | 0/0 |
| 57 | 0x7416 |  | — | — |  | — | 0/0 |
| 58 | 0x7436 |  | — | — |  | — | 0/0 |
| 59 | 0x7456 | 29 Transmission overload | 8:1224 | 9:6ac2 | ["01 15 01 00 be 01"] | [1,59] | 0/585 |
| 60 | 0x7476 | 30 Battery voltage | 8:1302 | 9:6ac2 | ["01 15 01 00 be 01"] | [1,60] | 0/585 |
| 61 | 0x7496 | 31 Exhaust gas | 8:135f | 9:6ad4 | ["01 14 01 00 bd 01"] | [1,61] | 0/585 |
| 62 | 0x74b6 | 32 Engine start | 8:0000 | 9:6aa4 | ["01 15 01 00 bd 01"] | [1,62] | 0/0 |
| 63 | 0x74d6 | 33 HFM voltage | 8:139e | 9:6af2 | ["01 14 01 00 bf 01"] | [1,63] | 0/585 |
| 64 | 0x74f6 | 34 Resistance trimming | 8:140e | 9:6af2 | ["01 14 01 00 bf 01"] | [1,64] | 0/585 |
| 65 | 0x7516 | 35 Ignition fault counter | 8:1546 | 9:6b52 | ["01 18 01 00 bd 01","01 18 02 00 bd 01","01 18 03 00 bd 01","01 18 04 00 bd 01","01 18 05 00 bd 01","01 18 06 00 bd 01"] | [1,65] | 570/585 |
| 66 | 0x7536 | 35 Ignition fault counter | 8:1546 | 9:6b76 | ["01 18 01 00 bd 01","01 18 02 00 bd 01","01 18 03 00 bd 01","01 18 04 00 bd 01"] | [1,66] | 570/585 |
| 67 | 0x7556 |  | — | — |  | — | 0/0 |
| 68 | 0x7576 |  | — | — |  | — | 0/0 |
| 69 | 0x7596 |  | — | — |  | — | 0/0 |
| 70 | 0x75b6 |  | — | — |  | — | 0/0 |
| 71 | 0x75d6 |  | — | — |  | — | 0/0 |
| 72 | 0x75f6 | 36 Ign. coil fault counter | 8:15c8 | 9:6b8e | ["01 18 02 00 bd 01","01 18 04 00 bd 01","01 18 06 00 bd 01"] | [1,72] | 571/585 |
| 73 | 0x7616 | 36 Ign. coil fault counter | 8:15c8 | 9:6ba0 | ["01 18 02 00 bd 01","01 18 04 00 bd 01"] | [1,73] | 571/585 |
| 74 | 0x7636 |  | — | — |  | — | 0/0 |
| 75 | 0x7656 |  | — | — |  | — | 0/0 |
| 76 | 0x7676 | 37 Ign. coil comb. time | 8:1611 | 9:6bac | ["01 14 01 00 bf 01","01 14 02 00 bf 01","01 14 03 00 bf 01"] | [1,76] | 572/585 |
| 77 | 0x7696 | 37 Ign. coil comb. time | 8:1611 | 9:6bbe | ["01 14 01 00 bf 01","01 14 02 00 bf 01"] | [1,77] | 572/585 |
| 78 | 0x76b6 |  | — | — |  | — | 0/0 |
| 79 | 0x76d6 |  | — | — |  | — | 0/0 |
| 80 | 0x76f6 | 38 Ign. coil comb. voltage | 8:187b | 9:6bca | ["01 16 01 00 bd 01","01 16 02 00 bd 01","01 16 03 00 bd 01"] | [1,80] | 573/585 |
| 81 | 0x7716 | 38 Ign. coil comb. voltage | 8:187b | 9:6bdc | ["01 16 01 00 bd 01","01 16 02 00 bd 01"] | [1,81] | 573/585 |
| 82 | 0x7736 |  | — | — |  | — | 0/0 |
| 83 | 0x7756 |  | — | — |  | — | 0/0 |
| 84 | 0x7776 | 39 Knock ignition angle | 8:18e8 | 9:6be8 | ["01 14 01 00 bd 01","01 14 02 00 bd 01","01 14 03 00 bd 01","01 14 04 00 bd 01","01 14 05 00 bd 01","01 14 06 00 bd 01"] | [1,84] | 574/585 |
| 85 | 0x7796 | 39 Knock ignition angle | 8:18e8 | 9:6c0c | ["01 14 01 00 bd 01","01 14 02 00 bd 01","01 14 03 00 bd 01","01 14 04 00 bd 01"] | [1,85] | 574/585 |
| 86 | 0x77b6 |  | — | — |  | — | 0/0 |
| 87 | 0x77d6 |  | — | — |  | — | 0/0 |
| 88 | 0x77f6 |  | — | — |  | — | 0/0 |
| 89 | 0x7816 |  | — | — |  | — | 0/0 |
| 90 | 0x7836 |  | — | — |  | — | 0/0 |
| 91 | 0x7856 | 40 Actual value potentiom. | 8:1ba3 | 9:6af2 | ["01 14 01 00 bf 01"] | — | 0/0 |
| 92 | 0x7876 | 41 Altitude pressure | 8:1c6b | 9:6ace | ["01 12 01 00 bd 01"] | [1,92] | 575/585 |
| 93 | 0x7896 | 42 Actuator output | 8:1cb2 | 9:6c24 | ["01 18 01 00 bd 01"] | — | 0/0 |
| 94 | 0x78b6 | 43 Regeneration valve | 8:1cce | 9:6abc | ["01 16 01 00 bd 01"] | [1,94] | 577/585 |
| 95 | 0x78d6 | 44 Load | 8:1d7d | 9:6ada | ["01 15 01 00 bd 01"] | — | 0/0 |
| 96 | 0x78f6 | 45 Lambda control average | 8:1da2 | 9:6ada | ["01 15 01 00 bd 01"] | — | 0/0 |
| 97 | 0x7916 | 46 Lambda control average | 8:1da2 | 9:6ada | ["01 15 01 00 bd 01"] | — | 0/0 |
| 98 | 0x7936 | 47 Lambda control 2 | 8:1e36 | 9:6c24 | ["01 18 01 00 bd 01"] | — | 0/0 |
| 99 | 0x7956 | 48 HFM identifier | 8:1cb2 | 9:6c2a | ["01 19 01 00 c0 01"] | — | 0/0 |
| 100 | 0x7976 | 49 Regeneration factor | 8:1e72 | 9:6c30 | ["01 14 01 00 be 01","01 14 02 00 be 01","01 14 03 00 be 01","01 14 04 00 be 01"] | — | 0/0 |
| 101 | 0x7996 |  | — | — |  | — | 0/0 |
| 102 | 0x79b6 |  | — | — |  | — | 0/0 |
| 103 | 0x79d6 |  | — | — |  | — | 0/0 |
| 104 | 0x79f6 | 50 CAT temperature | 8:2066 | 9:6ad4 | ["01 14 01 00 bd 01"] | — | 0/0 |
| 105 | 0x7a16 | 51 Fault code | 8:2088 | 9:6c48 | ["01 19 01 00 c0 01","01 19 02 00 c0 01","01 19 03 00 c0 01"] | — | 0/0 |
| 106 | 0x7a36 | 51 Fault code | 8:2088 | 9:6c60 | ["01 19 01 00 c0 01","01 19 02 00 c0 01","01 17 03 00 c0 01"] | — | 0/0 |
| 107 | 0x7a56 |  | — | — |  | — | 0/0 |
| 108 | 0x7a76 |  | — | — |  | — | 0/0 |
| 109 | 0x7a96 |  | — | — |  | — | 0/0 |
| 110 | 0x7ab6 | 52 Lambda shift | 8:2118 | 9:6c7e | ["01 13 01 00 bc 01"] | — | 0/0 |
| 111 | 0x7ad6 | 53 Sensor heater | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,111] | 0/585 |
| 112 | 0x7af6 | 54 Sensor heater down- | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,112] | 0/585 |
| 113 | 0x7b16 | 55 Clutch operated | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,113] | 0/585 |
| 114 | 0x7b36 | 56 Cruise control mode | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,114] | 0/585 |
| 115 | 0x7b56 | 57 Knock control enabled | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,115] | 578/585 |
| 116 | 0x7b76 | 58 Safet fuel cut-off | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,116] | 579/0 |
| 117 | 0x7b96 | 59 CAT heating idle speed | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,117] | 580/0 |
| 118 | 0x7bb6 | 60 Self-adjustment | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,118] | 581/585 |
| 119 | 0x7bd6 | 61 Transmission shiftpoint | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,119] | 0/0 |
| 120 | 0x7bf6 | 62 Multiple map adjust- | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | — | 0/0 |
| 121 | 0x7c16 | 63 RON index | 8:1cb2 | 9:6c84 | ["01 1b 01 00 bd 01"] | — | 0/0 |
| 122 | 0x7c36 | 51 Fault code | 8:2088 | 9:6c5a | ["01 19 01 00 c0 01"] | — | 0/0 |
| 123 | 0x7c56 | 51 Fault code | 8:2088 | 9:6c72 | ["01 19 01 00 c0 01","01 19 02 00 c0 01"] | — | 0/0 |
| 124 | 0x7c76 | 64 Drive authorization | 8:2171 | 9:6c8a | ["06 14 02 00 73 00","06 14 04 00 73 00","06 14 06 00 73 00","06 14 08 00 73 00"] | [1,124] | 556/0 |
| 125 | 0x7c96 |  | — | — |  | — | 0/0 |
| 126 | 0x7cb6 |  | — | — |  | — | 0/0 |
| 127 | 0x7cd6 |  | — | — |  | — | 0/0 |
| 128 | 0x7cf6 |  | — | — |  | — | 0/0 |
| 129 | 0x7d16 |  | — | — |  | — | 0/0 |
| 130 | 0x7d36 |  | — | — |  | — | 0/0 |
| 131 | 0x7d56 |  | — | — |  | — | 0/0 |
| 132 | 0x7d76 |  | — | — |  | — | 0/0 |
| 133 | 0x7d96 |  | — | — |  | — | 0/0 |
| 134 | 0x7db6 |  | — | — |  | — | 0/0 |
| 135 | 0x7dd6 |  | — | — |  | — | 0/0 |
| 136 | 0x7df6 |  | — | — |  | — | 0/0 |
| 137 | 0x7e16 | 65 Stop light switch | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,137] | 0/0 |
| 138 | 0x7e36 | 66 Stop light switch | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,138] | 0/0 |
| 139 | 0x7e56 | 67 Pushbutton switch | 8:21e7 | 9:6ca2 | ["06 13 01 00 a2 01"] | [1,139] | 0/0 |
| 140 | 0x7e76 | 68 Safety contact | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,140] | 0/0 |
| 141 | 0x7e96 | 69 Rpm increase | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,141] | 582/0 |
| 142 | 0x7eb6 | 70 ETS | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,142] | 0/0 |
| 143 | 0x7ed6 | 71 Cruise control shut-off | 8:2241 | 9:6ca8 | ["06 14 02 00 73 00","06 14 04 00 73 00","06 14 06 00 73 00","06 14 08 00 73 00"] | [1,143] | 583/0 |
| 144 | 0x7ef6 | 72 Cruise control shut-off | 8:22b7 | 9:6cc0 | ["06 14 03 00 73 00","06 14 05 00 73 00","06 14 07 00 73 00"] | [1,144] | 583/0 |
| 145 | 0x7f16 | 73 RON correction | 8:2312 | 9:6aec | ["06 14 01 00 10 00"] | [1,145] | 0/0 |
| 146 | 0x7f36 | 35 Ignition fault counter | 8:2381 | 9:6ba0 | ["01 18 02 00 bd 01","01 18 04 00 bd 01"] | [1,146] | 570/585 |
| 147 | 0x7f56 |  | — | — |  | — | 0/0 |
| 148 | 0x7f76 | 74 Air flap switchover | 8:23b0 | 9:6ac2 | ["01 15 01 00 be 01"] | [1,148] | 584/585 |
| 149 | 0x7f96 | 75 Supercharger coupling | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,149] | 0/585 |
| 150 | 0x7fb6 | 66 Stop light switch | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | [1,150] | 0/0 |
| 151 | 0x7fd6 | Supercharger coupling | — | 9:6aec | ["06 14 01 00 10 00"] | — | 0/0 |
| 152 | 0x7ff6 | 77 Supercharger efficiency | 8:23e6 | 9:6afe | ["01 17 01 00 bf 01"] | [1,152] | 0/585 |
| 153 | 0x8016 | 89 Front left vehicle | 8:11fe | 9:6b4c | ["01 13 01 00 bd 01"] | [1,153] | 0/0 |
| 154 | 0x8036 | Air flap switchover | — | 9:6cd2 | ["06 0d 01 00 72 00"] | — | 0/0 |
| 155 | 0x8056 | 78 Fan output request | 8:240b | 9:6ce4 | ["01 16 03 00 bc 01","01 16 05 00 bc 01"] | — | 0/0 |
| 156 | 0x8076 |  | — | — |  | — | 0/0 |
| 157 | 0x8096 | 79 Effective fan output | 8:1cb2 | 9:6cd8 | ["01 16 01 00 bc 01"] | — | 0/0 |
| 158 | 0x80b6 | 81 Operating point shift | 8:243a | 9:6aec | ["06 14 01 00 10 00"] | — | 0/0 |
| 159 | 0x80d6 | 82 Induced engine torque | 8:245f | 9:6cde | ["01 15 01 00 bc 01"] | — | 0/0 |
| 160 | 0x80f6 | 83 Drag torque | 8:245f | 9:6cde | ["01 15 01 00 bc 01"] | — | 0/0 |
| 161 | 0x8116 | 84 Driver input torque | 8:245f | 9:6cde | ["01 15 01 00 bc 01"] | — | 0/0 |
| 162 | 0x8136 | 85 Pedal position | 8:247e | 9:6cd8 | ["01 16 01 00 bc 01"] | — | 0/0 |
| 163 | 0x8156 | 86 Min. induced engine | 8:245f | 9:6cde | ["01 15 01 00 bc 01"] | — | 0/0 |
| 164 | 0x8176 | 87 Max. induced engine | 8:245f | 9:6cde | ["01 15 01 00 bc 01"] | — | 0/0 |
| 165 | 0x8196 | 88 Starter control | 8:0cdd | 9:6aec | ["06 14 01 00 10 00"] | — | 0/0 |
| 166 | 0x81b6 | Fuel pump | — | 9:6aec | ["06 14 01 00 10 00"] | — | 0/0 |
| 167 | 0x81d6 | Electric suction fan | — | 9:6cd8 | ["01 16 01 00 bc 01"] | — | 0/0 |
| 168 | 0x81f6 | Engine speed | 8:03c8 | 9:6b04 | ["01 11 01 00 bd 01"] | — | 0/0 |
| 169 | 0x8216 | Rpm limiter | — | 9:6aec | ["06 14 01 00 10 00"] | — | 0/0 |
| 170 | 0x8236 |  | — | — |  | — | 0/0 |
| 171 | 0x8256 | 43 Regeneration valve | 8:1cce | 9:6abc | ["01 16 01 00 bd 01"] | [1,171] | 577/585 |
| 172 | 0x8276 | 27 Vehicle speed signal | 8:11fe | 9:6b4c | ["01 13 01 00 bd 01"] | [1,172] | 0/585 |
| 173 | 0x8296 |  | — | — |  | — | 69/70 |

### 11.1 Инвентаризация функций пересчёта HHT-WIN

| Адрес сегмент:смещение | Число инструкций | Смещения ответа | Смещения идентификации | Ссылки на таблицы | Константы |
| --- | --- | --- | --- | --- | --- |
| 1038:0000 | 30 | [0,2] | [] | ["0xcd82"] | ["0x0","0x1","0x1040","0xffc4"] |
| 1038:0042 | 78 | [0,2,6,8,10,14] | [106] | [] | ["0x0","0x1","0x1040","0x19","0x1e","0x41","0x45"] |
| 1038:0120 | 151 | [0,2,6] | [106] | [] | ["0x0","0x1","0x1040","0x10a","0x1b2","0x2710","0x3","0x364","0x45","0x49","0x64","0x6400","0x9","0xa00","0xb","0xb200"] |
| 1038:0292 | 85 | [0,2,6] | [106] | [] | ["-0x6980","-0x7960","0x0","0x1","0x1040","0x312d","0x3600","0x4240","0x45","0x5","0x77","0x7736","0x98","0xf","0xf5e1","0xff9c","0xffce","0xffd3"] |
| 1038:0361 | 45 | [0,2] | [] | [] | ["0x0","0x1","0x1040","0x24","0x35","0x4240","0xa","0xa4e9","0xf","0xffb0"] |
| 1038:03c8 | 42 | [0,2,6] | [106] | [] | ["0x0","0x1","0x1040","0x19","0x1e","0x45"] |
| 1038:042f | 77 | [0,2,6] | [106] | [] | ["0x0","0x1","0x1040","0x19","0x45","0x8","0xa"] |
| 1038:04df | 142 | [0,2,6] | [106] | [] | ["0x0","0x1","0x1000","0x1040","0x1633","0x1b0","0x2710","0x2a","0x2a16","0x45","0x46","0x64","0x9394","0xbebc","0xc79e"] |
| 1038:0633 | 118 | [0,2,6] | [26,30,106] | [] | ["-0x7960","0x0","0x1","0x100","0x1040","0x113","0x312d","0x32","0x33","0x3e8","0x41","0x42","0x45","0x6980","0x7748","0x7f"] |
| 1038:0777 | 92 | [0,2,6] | [106] | [] | ["-0x6980","-0x7960","0x0","0x1","0x1040","0x312d","0x3600","0x4240","0x45","0x5","0x64","0x6980","0x77","0x7736","0x98","0xf","0xf5e1","0xf80","0xff9c"] |
| 1038:085e | 92 | [0,2,6] | [106] | [] | ["-0x6980","-0x7960","0x0","0x1","0x1040","0x312d","0x3600","0x4240","0x45","0x5","0x64","0x6980","0x77","0x7736","0x98","0xf","0xf5e1","0xf80","0xff9c"] |
| 1038:0945 | 127 | [0,2,6,10,14] | [106] | [] | ["-0x7960","0x0","0x1","0x1040","0x19","0x1e","0x226","0x45","0x64","0x76ab","0x8","0xa","0xec60","0xffff"] |
| 1038:0a87 | 30 | [0,2] | [] | ["0xcf82"] | ["0x0","0x1","0x1040","0xffc4"] |
| 1038:0ac9 | 193 | [0,2,6,8] | [106] | [] | ["0x0","0x1","0x1040","0x13","0x1318","0x136","0x16d","0x1794","0x179a","0x19a","0x1bd","0x1ea","0x2710","0x331c","0x3e8","0x45","0x7a1","0xa","0xa0","0xf0"] |
| 1038:0cdd | 136 | [0,2] | [106] | ["0xd182","0xd1c2","0xd202","0xd242"] | ["0x0","0x1","0x1040","0x2","0x45"] |
| 1038:0e46 | 112 | [0,2,6] | [106] | [] | ["0x0","0x1","0x1040","0x13","0x1318","0x179a","0x2710","0x3e8","0x4","0x45","0x7a1","0xa","0xb98c"] |
| 1038:0f5a | 234 | [0,2,4,6,8,10,12,14,18] | [] | [] | ["0x0","0x1","0x1040","0x2","0x7"] |
| 1038:11fe | 19 | [0,2] | [] | [] | ["0x0","0x1040","0xa","0xc"] |
| 1038:1224 | 94 | [0,2] | [] | [] | ["-0x7960","0x0","0x1","0x1040","0x13","0x1318","0x179a","0x263c","0x3e8","0xc4"] |
| 1038:1302 | 43 | [0] | [] | [] | ["0x0","0x1","0x1040","0x1ea3","0x268","0x2710","0x3e8"] |
| 1038:135f | 29 | [0] | [] | [] | ["0x0","0x1","0x1040","0x5","0xffe7"] |
| 1038:139e | 51 | [0,2] | [] | [] | ["0x0","0x1","0x1040","0x13","0x1318","0x179a","0x2710"] |
| 1038:140e | 128 | [0,2,6,8] | [106] | [] | ["0x0","0x1","0x1040","0x13","0x1318","0x1794","0x179a","0x2710","0x331c","0x3e8","0x45","0x7a1"] |
| 1038:1546 | 47 | [0,2,4,6,8,10] | [] | [] | ["0x0","0x1040","0x6"] |
| 1038:15c8 | 29 | [0,2,4] | [] | [] | ["0x0","0x1040","0x6"] |
| 1038:1611 | 249 | [0,2,4,6,8,10] | [106] | [] | ["0x0","0x1","0x1040","0x21","0x21e9","0x2710","0x3","0x49","0x6","0x64","0xa","0xb","0xd5","0xe900"] |
| 1038:187b | 44 | [0,2,4] | [] | [] | ["0x0","0x1040","0x6","0x7","0xa"] |
| 1038:18e8 | 243 | [0,2,4,6,8,10,12] | [] | [] | ["-0x7960","0x0","0x1","0x1040","0x4","0x6","0x76ab","0xa","0xffff"] |
| 1038:1ba3 | 84 | [0,2] | [106] | [] | ["0x0","0x1","0x1040","0x13","0x1318","0x179a","0x19c0","0x2710","0x46"] |
| 1038:1c6b | 32 | [0,2] | [] | [] | ["0x0","0x1","0x1040","0x5"] |
| 1038:1cb2 | 15 | [0] | [] | [] | ["0x0","0x1040"] |
| 1038:1cce | 74 | [0,2,4] | [106] | [] | ["0x0","0x1","0x1040","0x17","0x17d8","0x4240","0x5","0x50","0xa","0xd785","0xf","0xf5e1"] |
| 1038:1d7d | 19 | [0] | [] | [] | ["0x0","0x1040","0x5","0xa"] |
| 1038:1da2 | 64 | [0,2] | [] | [] | ["-0x6980","0x0","0x1","0x1040","0x3600","0x4240","0x5","0x77","0x7736","0x98","0xf","0xf5e1","0xf80","0xff9c"] |
| 1038:1e36 | 27 | [0] | [] | [] | ["0x0","0x100","0x1040","0x7f"] |
| 1038:1e72 | 192 | [0,2,4,6,8,10,12] | [106] | [] | ["-0x7960","0x0","0x1","0x1040","0x17","0x17d8","0x2710","0x312d","0x3e8","0x5","0x50","0xd785","0xf5e1"] |
| 1038:2066 | 17 | [0] | [] | [] | ["0x0","0x1040","0x5","0xffe7"] |
| 1038:2088 | 55 | [0,2,4,6] | [] | [] | ["0x0","0x1","0x1040"] |
| 1038:2118 | 39 | [0,2] | [] | [] | ["0x0","0x1","0x1040","0xa"] |
| 1038:2171 | 45 | [0] | [] | [] | ["0x1","0x1040","0x2"] |
| 1038:21e7 | 32 | [0] | [] | [] | ["0x1040"] |
| 1038:2241 | 45 | [0] | [] | [] | ["0x1","0x1040","0x2"] |
| 1038:22b7 | 36 | [0] | [] | [] | ["0x1","0x1040","0x2"] |
| 1038:2312 | 47 | [0,2,4] | [] | [] | ["0x0","0x1040","0x13","0x1318","0x179a","0x190","0x2710"] |
| 1038:2381 | 21 | [0,2] | [] | [] | ["0x0","0x1040"] |
| 1038:23b0 | 26 | [0] | [] | [] | ["0x0","0x1040","0x3e8","0xdbc"] |
| 1038:23e6 | 19 | [0] | [] | [] | ["0x0","0x1040","0x64","0x80"] |
| 1038:240b | 21 | [0,2] | [] | [] | ["0x0","0x1040"] |
| 1038:243a | 18 | [0] | [] | [] | ["0x0","0x1","0x1040","0x2"] |
| 1038:245f | 16 | [0] | [] | [] | ["0x0","0x1040","0x3"] |
| 1038:247e | 19 | [0] | [] | [] | ["0x0","0x1040","0xa"] |

Поле `conv` указывает на восстановленный адрес функции в 16-битной DLL. Входы/знаковые арифметические преобразования для каждого адреса ещё нуждаются в посимвольном восстановлении по Ghidra листингам. Инвентаризация констант не является сама по себе точной формулой.
## 12. Ошибки: банки, биты, каталоги, кодировки

### 12.1 Адреса банков и блокирующее противоречие

| Источник | F1:00–0F | F1:10–1F | Прочие адреса | Статус |
| --- | --- | --- | --- | --- |
| HFMreader (dtc_protocol.pyc) | Stored | Current | 16 байт на банк (128 бит) | КОД |
| HFMScan (commands.json) | Current | Stored | для 129–152 бит дополнительно 30–32 / 40–42 | КОД (по экспортам) |
| HHT-WIN (sg_table_c814) | шаблон AT для F1:00–0F | шаблон AT для F1:10–1F | семантика зависит от сценария обработки и UI | ДАННЫЕ, статус требует подтверждения |

**НЕ УСТАНОВЛЕНО**, какой диапазон означает «текущие»/«сохранённые» у конкретного ЭБУ. В реализации следует сохранять два независимых банка `bank0` и `bank1`, не маркируя пользователю Current/Stored до подтверждения. Верифицировать включением/устранением известной неисправности и повторным чтением на автомобиле, не сбрасывая ошибки автоматически.
### 12.2 Базовая карта HFMreader: все 152 битовых позиции

Нормализованная позиция `position = byte_index*8 + bit_index + 1`. Таблица ниже — данные **HFMreader**, а не доказательство доступности всех 152 позиций по любому диапазону F1.
| Позиция | Банк 0 индекс | Банк 1 индекс | Бит | Маска | DTC | Доступность в 16 байтах | Описание |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 00 | 10 | 0 | 0x01 | 2 | yes | Coolant temperature sensor: short circuit. |
| 2 | 00 | 10 | 1 | 0x02 | 3 | yes | Coolant temperature sensor: open circuit. |
| 3 | 00 | 10 | 2 | 0x04 | 4 | yes | Coolant temperature sensor: implausible signal. |
| 4 | 00 | 10 | 3 | 0x08 | 6 | yes | Intake air temperature sensor: short circuit. |
| 5 | 00 | 10 | 4 | 0x10 | 7 | yes | Intake air temperature sensor: open circuit. |
| 6 | 00 | 10 | 5 | 0x20 | 9 | yes | Mass airflow sensor: airflow implausibly high. |
| 7 | 00 | 10 | 6 | 0x40 | 10 | yes | Mass airflow sensor: open circuit. |
| 8 | 00 | 10 | 7 | 0x80 | 11 | yes | Closed-throttle switch: throttle angle implausibly high. |
| 9 | 01 | 11 | 0 | 0x01 | 118 | yes | Definition not yet verified. |
| 10 | 01 | 11 | 1 | 0x02 | 12 | yes | Closed-throttle switch: airflow implausibly high. |
| 11 | 01 | 11 | 2 | 0x04 | 113 | yes | Engine ECU has not been coded. |
| 12 | 01 | 11 | 3 | 0x08 | 14 | yes | Throttle-position potentiometer: value too high. |
| 13 | 01 | 11 | 4 | 0x10 | 15 | yes | Throttle-position potentiometer: value too low. |
| 14 | 01 | 11 | 5 | 0x20 | 17 | yes | Throttle-drive potentiometer: value too high. |
| 15 | 01 | 11 | 6 | 0x40 | 18 | yes | Throttle-drive potentiometer: value too low. |
| 16 | 01 | 11 | 7 | 0x80 | 20 | yes | Idle-speed control: lower adjustment limit reached. |
| 17 | 02 | 12 | 0 | 0x01 | 21 | yes | Idle-speed control: upper adjustment limit reached. |
| 18 | 02 | 12 | 1 | 0x02 | 22 | yes | Cruise control/electronic accelerator reports emergency operation. |
| 19 | 02 | 12 | 2 | 0x04 | 23 | yes | Upstream oxygen sensor: voltage too high. |
| 20 | 02 | 12 | 3 | 0x08 | 24 | yes | Upstream oxygen sensor: cold or open circuit. |
| 21 | 02 | 12 | 4 | 0x10 | 25 | yes | Upstream oxygen sensor: implausible voltage. |
| 22 | 02 | 12 | 5 | 0x20 | 26 | yes | Downstream oxygen sensor: voltage too high. |
| 23 | 02 | 12 | 6 | 0x40 | 27 | yes | Downstream oxygen sensor: cold or open circuit. |
| 24 | 02 | 12 | 7 | 0x80 | 28 | yes | Downstream oxygen sensor: implausible voltage. |
| 25 | 03 | 13 | 0 | 0x01 | 29 | yes | Upstream oxygen-sensor heater: current too low. |
| 26 | 03 | 13 | 1 | 0x02 | 30 | yes | Upstream oxygen-sensor heater: current too high. |
| 27 | 03 | 13 | 2 | 0x04 | 31 | yes | Upstream oxygen-sensor heater: short circuit. |
| 28 | 03 | 13 | 3 | 0x08 | 32 | yes | Downstream oxygen-sensor heater: current too low. |
| 29 | 03 | 13 | 4 | 0x10 | 33 | yes | Downstream oxygen-sensor heater: current too high. |
| 30 | 03 | 13 | 5 | 0x20 | 34 | yes | Downstream oxygen-sensor heater: short circuit. |
| 31 | 03 | 13 | 6 | 0x40 | 35 | yes | Mixture too lean; oxygen-sensor control at enrichment limit. |
| 32 | 03 | 13 | 7 | 0x80 | 36 | yes | Mixture too rich; oxygen-sensor control at leaning limit. |
| 33 | 04 | 14 | 0 | 0x01 | 37 | yes | Cylinder 1 injector: short to battery positive. |
| 34 | 04 | 14 | 1 | 0x02 | 38 | yes | Cylinder 1 injector: open circuit or short to ground. |
| 35 | 04 | 14 | 2 | 0x04 | 39 | yes | Cylinder 2 injector: short to battery positive. |
| 36 | 04 | 14 | 3 | 0x08 | 40 | yes | Cylinder 2 injector: open circuit or short to ground. |
| 37 | 04 | 14 | 4 | 0x10 | 41 | yes | Cylinder 3 injector: short to battery positive. |
| 38 | 04 | 14 | 5 | 0x20 | 42 | yes | Cylinder 3 injector: open circuit or short to ground. |
| 39 | 04 | 14 | 6 | 0x40 | 43 | yes | Cylinder 4 injector: short to battery positive. |
| 40 | 04 | 14 | 7 | 0x80 | 44 | yes | Cylinder 4 injector: open circuit or short to ground. |
| 41 | 05 | 15 | 0 | 0x01 | 45 | yes | Cylinder 5 injector: short to battery positive. |
| 42 | 05 | 15 | 1 | 0x02 | 46 | yes | Cylinder 5 injector: open circuit or short to ground. |
| 43 | 05 | 15 | 2 | 0x04 | 47 | yes | Cylinder 6 injector: short to battery positive. |
| 44 | 05 | 15 | 3 | 0x08 | 48 | yes | Cylinder 6 injector: open circuit or short to ground. |
| 45 | 05 | 15 | 4 | 0x10 | 49 | yes | Mixture adaptation at idle: too rich. |
| 46 | 05 | 15 | 5 | 0x20 | 50 | yes | Mixture adaptation at idle: too lean. |
| 47 | 05 | 15 | 6 | 0x40 | 51 | yes | Mixture adaptation at lower partial load: too rich. |
| 48 | 05 | 15 | 7 | 0x80 | 52 | yes | Mixture adaptation at lower partial load: too lean. |
| 49 | 06 | 16 | 0 | 0x01 | 53 | yes | Mixture adaptation at upper partial load: too rich. |
| 50 | 06 | 16 | 1 | 0x02 | 54 | yes | Mixture adaptation at upper partial load: too lean. |
| 51 | 06 | 16 | 2 | 0x04 | 61 | yes | Ignition output/coil 3: cylinder 1 misfire. |
| 52 | 06 | 16 | 3 | 0x08 | 62 | yes | Ignition output/coil 3: cylinder 6 misfire. |
| 53 | 06 | 16 | 4 | 0x10 | 63 | yes | Ignition output/coil 3: required current not reached. |
| 54 | 06 | 16 | 5 | 0x20 | 55 | yes | Ignition output/coil 1: cylinder 2 misfire. |
| 55 | 06 | 16 | 6 | 0x40 | 56 | yes | Ignition output/coil 1: cylinder 5 misfire. |
| 56 | 06 | 16 | 7 | 0x80 | 57 | yes | Ignition output/coil 1: required current not reached. |
| 57 | 07 | 17 | 0 | 0x01 | 58 | yes | Ignition output/coil 2: cylinder 3 misfire. |
| 58 | 07 | 17 | 1 | 0x02 | 59 | yes | Ignition output/coil 2: cylinder 4 misfire. |
| 59 | 07 | 17 | 2 | 0x04 | 60 | yes | Ignition output/coil 2: required current not reached. |
| 60 | 07 | 17 | 3 | 0x08 | 64 | yes | Crankshaft position signal: missing or implausible. |
| 61 | 07 | 17 | 4 | 0x10 | 65 | yes | Crankshaft reference: missing magnet or implausible tooth count. |
| 62 | 07 | 17 | 5 | 0x20 | 66 | yes | Crankshaft-derived engine speed: implausibly high. |
| 63 | 07 | 17 | 6 | 0x40 | 67 | yes | Camshaft position signal: missing or implausible. |
| 64 | 07 | 17 | 7 | 0x80 | 68 | yes | HFM trimming plug: short to ground. |
| 65 | 08 | 18 | 0 | 0x01 | 69 | yes | HFM trimming plug: open circuit or short to positive. |
| 66 | 08 | 18 | 1 | 0x02 | 70 | yes | TN engine-speed output: short to ground. |
| 67 | 08 | 18 | 2 | 0x04 | 71 | yes | TN engine-speed output: short to battery positive. |
| 68 | 08 | 18 | 3 | 0x08 | 72 | yes | Vehicle-speed signal: not detected. |
| 69 | 08 | 18 | 4 | 0x10 | 73 | yes | Vehicle-speed signal: implausibly high. |
| 70 | 08 | 18 | 5 | 0x20 | 74 | yes | PSV relay K3/1: short to positive. |
| 71 | 08 | 18 | 6 | 0x40 | 75 | yes | Definition not yet verified. |
| 72 | 08 | 18 | 7 | 0x80 | 76 | yes | Fuel-pump relay: open or short circuit. |
| 73 | 09 | 19 | 0 | 0x01 | 119 | yes | Definition not yet verified. |
| 74 | 09 | 19 | 1 | 0x02 | 77 | yes | CO potentiometer: short to positive. |
| 75 | 09 | 19 | 2 | 0x04 | 79 | yes | Knock sensor 1: open circuit. |
| 76 | 09 | 19 | 3 | 0x08 | 80 | yes | Knock sensor 2: open circuit. |
| 77 | 09 | 19 | 4 | 0x10 | 81 | yes | Ignition retard limit reached on one or more cylinders. |
| 78 | 09 | 19 | 5 | 0x20 | 82 | yes | Ignition-angle difference between cylinders exceeds 6 crankshaft degrees. |
| 79 | 09 | 19 | 6 | 0x40 | 83 | yes | Engine ECU: knock-evaluation circuit fault. |
| 80 | 09 | 19 | 7 | 0x80 | 84 | yes | Transient mixture-adaptation fault at idle or partial load. |
| 81 | 0A | 1A | 0 | 0x01 | 85 | yes | Secondary-air pump valve/clutch/relay circuit: open or short circuit. |
| 82 | 0A | 1A | 1 | 0x02 | 239 | yes | (not in catalog) |
| 83 | 0A | 1A | 2 | 0x04 | 240 | yes | (not in catalog) |
| 84 | 0A | 1A | 3 | 0x08 | 86 | yes | Purge valve: open or short circuit. |
| 85 | 0A | 1A | 4 | 0x10 | 87 | yes | Purge valve: short to battery positive. |
| 86 | 0A | 1A | 5 | 0x20 | 88 | yes | Upshift-delay valve: open or short circuit. |
| 87 | 0A | 1A | 6 | 0x40 | 123 | yes | Definition not yet verified. |
| 88 | 0A | 1A | 7 | 0x80 | 124 | yes | Definition not yet verified. |
| 89 | 0B | 1B | 0 | 0x01 | 89 | yes | Camshaft timing solenoid: short to battery positive. |
| 90 | 0B | 1B | 1 | 0x02 | 90 | yes | Camshaft timing solenoid: open circuit or short to ground. |
| 91 | 0B | 1B | 2 | 0x04 | 91 | yes | EGR valve control: short to battery positive. |
| 92 | 0B | 1B | 3 | 0x08 | 92 | yes | EGR valve control: open circuit or short to ground. |
| 93 | 0B | 1B | 4 | 0x10 | 93 | yes | Transmission overload switch: short to ground. |
| 94 | 0B | 1B | 5 | 0x20 | 94 | yes | Transmission overload switch closed while second gear is detected. |
| 95 | 0B | 1B | 6 | 0x40 | 95 | yes | Transmission overload switch open while second gear is detected. |
| 96 | 0B | 1B | 7 | 0x80 | 96 | yes | Transmission overload switch: implausible state. |
| 97 | 0C | 1C | 0 | 0x01 | 97 | yes | CAN communication fault from the engine ECU. |
| 98 | 0C | 1C | 1 | 0x02 | 98 | yes | CAN communication fault from the ASR controller. |
| 99 | 0C | 1C | 2 | 0x04 | 116 | yes | CAN communication fault from the remote central-locking controller. |
| 100 | 0C | 1C | 3 | 0x08 | 243 | yes | (not in catalog) |
| 101 | 0C | 1C | 4 | 0x10 | 99 | yes | CAN communication fault from the accelerator/cruise/idle controller. |
| 102 | 0C | 1C | 5 | 0x20 | 100 | yes | CAN communication fault from the OBD-II diagnostic module. |
| 103 | 0C | 1C | 6 | 0x40 | 244 | yes | (not in catalog) |
| 104 | 0C | 1C | 7 | 0x80 | 117 | yes | Starting/remote-locking condition. Detailed condition not yet verified. |
| 105 | 0D | 1D | 0 | 0x01 | 101 | yes | Starter signal at terminal 50: missing. |
| 106 | 0D | 1D | 1 | 0x02 | 102 | yes | Catalyst thermocouple: temperature too high. |
| 107 | 0D | 1D | 2 | 0x04 | 103 | yes | Catalyst thermocouple: temperature too low. |
| 108 | 0D | 1D | 3 | 0x08 | 104 | yes | Accelerator/cruise-control fuel safety shutoff is active. |
| 109 | 0D | 1D | 4 | 0x10 | 125 | yes | Engine ECU fault. |
| 110 | 0D | 1D | 5 | 0x20 | 105 | yes | Resonance-intake valve: short to battery positive. |
| 111 | 0D | 1D | 6 | 0x40 | 106 | yes | Resonance-intake valve: open circuit or short to ground. |
| 112 | 0D | 1D | 7 | 0x80 | 107 | yes | Ignition control fault. Detailed condition not yet verified. |
| 113 | 0E | 1E | 0 | 0x01 | 114 | yes | Engine ECU identification: invalid. |
| 114 | 0E | 1E | 1 | 0x02 | 108 | yes | Downstream oxygen-heater relay: short to battery positive. |
| 115 | 0E | 1E | 2 | 0x04 | 109 | yes | Downstream oxygen-heater relay: open circuit or short to ground. |
| 116 | 0E | 1E | 3 | 0x08 | 115 | yes | Engine ECU coding bytes: invalid. |
| 117 | 0E | 1E | 4 | 0x10 | 126 | yes | Engine ECU fault. |
| 118 | 0E | 1E | 5 | 0x20 | 110 | yes | Engine ECU supply at terminal 87U: implausible voltage. |
| 119 | 0E | 1E | 6 | 0x40 | 111 | yes | Engine ECU supply at terminal 87U: undervoltage. |
| 120 | 0E | 1E | 7 | 0x80 | 112 | yes | Engine ECU fault. |
| 121 | 0F | 1F | 0 | 0x01 | 5 | yes | Coolant temperature sensor: intermittent connection. |
| 122 | 0F | 1F | 1 | 0x02 | 8 | yes | Intake air temperature sensor: intermittent connection. |
| 123 | 0F | 1F | 2 | 0x04 | 13 | yes | Closed-throttle switch: intermittent connection. |
| 124 | 0F | 1F | 3 | 0x08 | 16 | yes | Throttle-position potentiometer: intermittent connection. |
| 125 | 0F | 1F | 4 | 0x10 | 19 | yes | Throttle-drive potentiometer: intermittent connection. |
| 126 | 0F | 1F | 5 | 0x20 | 78 | yes | CO potentiometer: intermittent connection. |
| 127 | 0F | 1F | 6 | 0x40 | 247 | yes | (not in catalog) |
| 128 | 0F | 1F | 7 | 0x80 | 127 | yes | Idle and cruise/idle actuators are interchanged. |
| 129 |  |  | 0 | 0x01 | 128 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 130 |  |  | 1 | 0x02 | 129 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 131 |  |  | 2 | 0x04 | 130 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 132 |  |  | 3 | 0x08 | 131 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 133 |  |  | 4 | 0x10 | 132 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 134 |  |  | 5 | 0x20 | 156 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 135 |  |  | 6 | 0x40 | 157 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 136 |  |  | 7 | 0x80 | 160 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 137 |  |  | 0 | 0x01 | 133 | no (needs >16 bytes per bank) | Accelerator/cruise/idle actuator fault. |
| 138 |  |  | 1 | 0x02 | 134 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 139 |  |  | 2 | 0x04 | 135 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 140 |  |  | 3 | 0x08 | 161 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 141 |  |  | 4 | 0x10 | 136 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 142 |  |  | 5 | 0x20 | 253 | no (needs >16 bytes per bank) | (not in catalog) |
| 143 |  |  | 6 | 0x40 | 137 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 144 |  |  | 7 | 0x80 | 138 | no (needs >16 bytes per bank) | Accelerator/cruise/idle actuator fault. |
| 145 |  |  | 0 | 0x01 | 139 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 146 |  |  | 1 | 0x02 | 140 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 147 |  |  | 2 | 0x04 | 141 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 148 |  |  | 3 | 0x08 | 142 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 149 |  |  | 4 | 0x10 | 143 | no (needs >16 bytes per bank) | Definition not yet verified. |
| 150 |  |  | 5 | 0x20 | 144 | no (needs >16 bytes per bank) | Engine ECU fault. |
| 151 |  |  | 6 | 0x40 | 254 | no (needs >16 bytes per bank) | (not in catalog) |
| 152 |  |  | 7 | 0x80 | 255 | no (needs >16 bytes per bank) | (not in catalog) |

### 12.3 Полный каталог HFMreader (163 записи включая метки)

| MB № | HHT label | Тип | Описание на русском | Английский оригинал | Подтверждение | Применимость | Расположение |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 000 | application_label | Неопознанная неисправность (метка приложения, не код ЭБУ). | Unidentified fault. | application_label | Application presentation only; not established as a digital HHT code. | not reachable in default mapping |
| 1 | 001 | application_label | Неисправностей не обнаружено (метка приложения: пустая битовая карта). | No faults detected. | application_label | Application presentation only; not established as a digital HHT code. | not reachable in default mapping |
| 2 | 002 | hfm_dtc | Датчик температуры ОЖ: короткое замыкание. | Coolant temperature sensor: short circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos1: stored 00/current 10 mask 0x01 |
| 3 | 003 | hfm_dtc | Датчик температуры ОЖ: обрыв цепи. | Coolant temperature sensor: open circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos2: stored 00/current 10 mask 0x02 |
| 4 | 004 | hfm_dtc | Датчик температуры ОЖ: неправдоподобный сигнал. | Coolant temperature sensor: implausible signal. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos3: stored 00/current 10 mask 0x04 |
| 5 | 005 | hfm_dtc | Датчик температуры ОЖ: перемежающийся контакт. | Coolant temperature sensor: intermittent connection. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos121: stored 0F/current 1F mask 0x01 |
| 6 | 006 | hfm_dtc | Датчик температуры всасываемого воздуха: короткое замыкание. | Intake air temperature sensor: short circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos4: stored 00/current 10 mask 0x08 |
| 7 | 007 | hfm_dtc | Датчик температуры всасываемого воздуха: обрыв цепи. | Intake air temperature sensor: open circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos5: stored 00/current 10 mask 0x10 |
| 8 | 008 | hfm_dtc | Датчик температуры всасываемого воздуха: перемежающийся контакт. | Intake air temperature sensor: intermittent connection. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos122: stored 0F/current 1F mask 0x02 |
| 9 | 009 | hfm_dtc | Датчик массового расхода воздуха: расход неправдоподобно высокий. | Mass airflow sensor: airflow implausibly high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos6: stored 00/current 10 mask 0x20 |
| 10 | 010 | hfm_dtc | Датчик массового расхода воздуха: обрыв цепи. | Mass airflow sensor: open circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos7: stored 00/current 10 mask 0x40 |
| 11 | 011 | hfm_dtc | Выключатель холостого хода: угол дросселя неправдоподобно велик. | Closed-throttle switch: throttle angle implausibly high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos8: stored 00/current 10 mask 0x80 |
| 12 | 012 | hfm_dtc | Выключатель холостого хода: расход воздуха неправдоподобно высок. | Closed-throttle switch: airflow implausibly high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos10: stored 01/current 11 mask 0x02 |
| 13 | 013 | hfm_dtc | Выключатель холостого хода: перемежающийся контакт. | Closed-throttle switch: intermittent connection. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos123: stored 0F/current 1F mask 0x04 |
| 14 | 014 | hfm_dtc | Потенциометр положения дросселя: значение слишком велико. | Throttle-position potentiometer: value too high. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos12: stored 01/current 11 mask 0x08 |
| 15 | 015 | hfm_dtc | Потенциометр положения дросселя: значение слишком мало. | Throttle-position potentiometer: value too low. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos13: stored 01/current 11 mask 0x10 |
| 16 | 016 | hfm_dtc | Потенциометр положения дросселя: перемежающийся контакт. | Throttle-position potentiometer: intermittent connection. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos124: stored 0F/current 1F mask 0x08 |
| 17 | 017 | hfm_dtc | Потенциометр привода дросселя: значение слишком велико. | Throttle-drive potentiometer: value too high. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos14: stored 01/current 11 mask 0x20 |
| 18 | 018 | hfm_dtc | Потенциометр привода дросселя: значение слишком мало. | Throttle-drive potentiometer: value too low. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos15: stored 01/current 11 mask 0x40 |
| 19 | 019 | hfm_dtc | Потенциометр привода дросселя: перемежающийся контакт. | Throttle-drive potentiometer: intermittent connection. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos125: stored 0F/current 1F mask 0x10 |
| 20 | 020 | hfm_dtc | Регулирование ХХ: достигнут нижний предел регулирования. | Idle-speed control: lower adjustment limit reached. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos16: stored 01/current 11 mask 0x80 |
| 21 | 021 | hfm_dtc | Регулирование ХХ: достигнут верхний предел регулирования. | Idle-speed control: upper adjustment limit reached. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos17: stored 02/current 12 mask 0x01 |
| 22 | 022 | hfm_dtc | Круиз-контроль/электронная педаль сообщает аварийный режим. | Cruise control/electronic accelerator reports emergency operation. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos18: stored 02/current 12 mask 0x02 |
| 23 | 023 | hfm_dtc | Верхний (передний) лямбда-зонд: напряжение слишком высокое. | Upstream oxygen sensor: voltage too high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos19: stored 02/current 12 mask 0x04 |
| 24 | 024 | hfm_dtc | Верхний (передний) лямбда-зонд: холодный или обрыв цепи. | Upstream oxygen sensor: cold or open circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos20: stored 02/current 12 mask 0x08 |
| 25 | 025 | hfm_dtc | Верхний (передний) лямбда-зонд: неправдоподобное напряжение. | Upstream oxygen sensor: implausible voltage. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos21: stored 02/current 12 mask 0x10 |
| 26 | 026 | hfm_dtc | Нижний (задний) лямбда-зонд: напряжение слишком высокое. | Downstream oxygen sensor: voltage too high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos22: stored 02/current 12 mask 0x20 |
| 27 | 027 | hfm_dtc | Нижний (задний) лямбда-зонд: холодный или обрыв цепи. | Downstream oxygen sensor: cold or open circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos23: stored 02/current 12 mask 0x40 |
| 28 | 028 | hfm_dtc | Нижний (задний) лямбда-зонд: неправдоподобное напряжение. | Downstream oxygen sensor: implausible voltage. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos24: stored 02/current 12 mask 0x80 |
| 29 | 029 | hfm_dtc | Подогрев верхнего лямбда-зонда: ток слишком мал. | Upstream oxygen-sensor heater: current too low. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos25: stored 03/current 13 mask 0x01 |
| 30 | 030 | hfm_dtc | Подогрев верхнего лямбда-зонда: ток слишком велик. | Upstream oxygen-sensor heater: current too high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos26: stored 03/current 13 mask 0x02 |
| 31 | 031 | hfm_dtc | Подогрев верхнего лямбда-зонда: короткое замыкание. | Upstream oxygen-sensor heater: short circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos27: stored 03/current 13 mask 0x04 |
| 32 | 032 | hfm_dtc | Подогрев нижнего лямбда-зонда: ток слишком мал. | Downstream oxygen-sensor heater: current too low. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos28: stored 03/current 13 mask 0x08 |
| 33 | 033 | hfm_dtc | Подогрев нижнего лямбда-зонда: ток слишком велик. | Downstream oxygen-sensor heater: current too high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos29: stored 03/current 13 mask 0x10 |
| 34 | 034 | hfm_dtc | Подогрев нижнего лямбда-зонда: короткое замыкание. | Downstream oxygen-sensor heater: short circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos30: stored 03/current 13 mask 0x20 |
| 35 | 035 | hfm_dtc | Смесь слишком бедная; лямбда-регулирование на пределе обогащения. | Mixture too lean; oxygen-sensor control at enrichment limit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos31: stored 03/current 13 mask 0x40 |
| 36 | 036 | hfm_dtc | Смесь слишком богатая; лямбда-регулирование на пределе обеднения. | Mixture too rich; oxygen-sensor control at leaning limit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos32: stored 03/current 13 mask 0x80 |
| 37 | 037 | hfm_dtc | Форсунка цилиндра 1: замыкание на плюс АКБ. | Cylinder 1 injector: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos33: stored 04/current 14 mask 0x01 |
| 38 | 038 | hfm_dtc | Форсунка цилиндра 1: обрыв или замыкание на массу. | Cylinder 1 injector: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos34: stored 04/current 14 mask 0x02 |
| 39 | 039 | hfm_dtc | Форсунка цилиндра 2: замыкание на плюс АКБ. | Cylinder 2 injector: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos35: stored 04/current 14 mask 0x04 |
| 40 | 040 | hfm_dtc | Форсунка цилиндра 2: обрыв или замыкание на массу. | Cylinder 2 injector: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos36: stored 04/current 14 mask 0x08 |
| 41 | 041 | hfm_dtc | Форсунка цилиндра 3: замыкание на плюс АКБ. | Cylinder 3 injector: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos37: stored 04/current 14 mask 0x10 |
| 42 | 042 | hfm_dtc | Форсунка цилиндра 3: обрыв или замыкание на массу. | Cylinder 3 injector: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos38: stored 04/current 14 mask 0x20 |
| 43 | 043 | hfm_dtc | Форсунка цилиндра 4: замыкание на плюс АКБ. | Cylinder 4 injector: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos39: stored 04/current 14 mask 0x40 |
| 44 | 044 | hfm_dtc | Форсунка цилиндра 4: обрыв или замыкание на массу. | Cylinder 4 injector: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos40: stored 04/current 14 mask 0x80 |
| 45 | 045 | hfm_dtc | Форсунка цилиндра 5: замыкание на плюс АКБ. | Cylinder 5 injector: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos41: stored 05/current 15 mask 0x01 |
| 46 | 046 | hfm_dtc | Форсунка цилиндра 5: обрыв или замыкание на массу. | Cylinder 5 injector: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos42: stored 05/current 15 mask 0x02 |
| 47 | 047 | hfm_dtc | Форсунка цилиндра 6: замыкание на плюс АКБ. | Cylinder 6 injector: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos43: stored 05/current 15 mask 0x04 |
| 48 | 048 | hfm_dtc | Форсунка цилиндра 6: обрыв или замыкание на массу. | Cylinder 6 injector: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos44: stored 05/current 15 mask 0x08 |
| 49 | 049 | hfm_dtc | Адаптация смеси на ХХ: слишком богатая. | Mixture adaptation at idle: too rich. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos45: stored 05/current 15 mask 0x10 |
| 50 | 050 | hfm_dtc | Адаптация смеси на ХХ: слишком бедная. | Mixture adaptation at idle: too lean. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos46: stored 05/current 15 mask 0x20 |
| 51 | 051 | hfm_dtc | Адаптация смеси в нижней частичной нагрузке: слишком богатая. | Mixture adaptation at lower partial load: too rich. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos47: stored 05/current 15 mask 0x40 |
| 52 | 052 | hfm_dtc | Адаптация смеси в нижней частичной нагрузке: слишком бедная. | Mixture adaptation at lower partial load: too lean. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos48: stored 05/current 15 mask 0x80 |
| 53 | 053 | hfm_dtc | Адаптация смеси в верхней частичной нагрузке: слишком богатая. | Mixture adaptation at upper partial load: too rich. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos49: stored 06/current 16 mask 0x01 |
| 54 | 054 | hfm_dtc | Адаптация смеси в верхней частичной нагрузке: слишком бедная. | Mixture adaptation at upper partial load: too lean. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos50: stored 06/current 16 mask 0x02 |
| 55 | 055 | hfm_dtc | Выход зажигания/катушка 1: пропуски в цилиндре 2. | Ignition output/coil 1: cylinder 2 misfire. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos54: stored 06/current 16 mask 0x20 |
| 56 | 056 | hfm_dtc | Выход зажигания/катушка 1: пропуски в цилиндре 5. | Ignition output/coil 1: cylinder 5 misfire. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos55: stored 06/current 16 mask 0x40 |
| 57 | 057 | hfm_dtc | Выход зажигания/катушка 1: не достигнут требуемый ток. | Ignition output/coil 1: required current not reached. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos56: stored 06/current 16 mask 0x80 |
| 58 | 058 | hfm_dtc | Выход зажигания/катушка 2: пропуски в цилиндре 3. | Ignition output/coil 2: cylinder 3 misfire. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos57: stored 07/current 17 mask 0x01 |
| 59 | 059 | hfm_dtc | Выход зажигания/катушка 2: пропуски в цилиндре 4. | Ignition output/coil 2: cylinder 4 misfire. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos58: stored 07/current 17 mask 0x02 |
| 60 | 060 | hfm_dtc | Выход зажигания/катушка 2: не достигнут требуемый ток. | Ignition output/coil 2: required current not reached. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos59: stored 07/current 17 mask 0x04 |
| 61 | 061 | hfm_dtc | Выход зажигания/катушка 3: пропуски в цилиндре 1. | Ignition output/coil 3: cylinder 1 misfire. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos51: stored 06/current 16 mask 0x04 |
| 62 | 062 | hfm_dtc | Выход зажигания/катушка 3: пропуски в цилиндре 6. | Ignition output/coil 3: cylinder 6 misfire. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos52: stored 06/current 16 mask 0x08 |
| 63 | 063 | hfm_dtc | Выход зажигания/катушка 3: не достигнут требуемый ток. | Ignition output/coil 3: required current not reached. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos53: stored 06/current 16 mask 0x10 |
| 64 | 064 | hfm_dtc | Сигнал положения коленвала: отсутствует или неправдоподобен. | Crankshaft position signal: missing or implausible. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos60: stored 07/current 17 mask 0x08 |
| 65 | 065 | hfm_dtc | Опорная метка коленвала: нет магнита или неправдоподобное число зубьев. | Crankshaft reference: missing magnet or implausible tooth count. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos61: stored 07/current 17 mask 0x10 |
| 66 | 066 | hfm_dtc | Частота вращения по коленвалу: неправдоподобно высокая. | Crankshaft-derived engine speed: implausibly high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos62: stored 07/current 17 mask 0x20 |
| 67 | 067 | hfm_dtc | Сигнал положения распредвала: отсутствует или неправдоподобен. | Camshaft position signal: missing or implausible. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos63: stored 07/current 17 mask 0x40 |
| 68 | 068 | hfm_dtc | Корректирующий штекер HFM: замыкание на массу. | HFM trimming plug: short to ground. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos64: stored 07/current 17 mask 0x80 |
| 69 | 069 | hfm_dtc | Корректирующий штекер HFM: обрыв или замыкание на плюс. | HFM trimming plug: open circuit or short to positive. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos65: stored 08/current 18 mask 0x01 |
| 70 | 070 | hfm_dtc | Выход оборотов TN: замыкание на массу. | TN engine-speed output: short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos66: stored 08/current 18 mask 0x02 |
| 71 | 071 | hfm_dtc | Выход оборотов TN: замыкание на плюс АКБ. | TN engine-speed output: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos67: stored 08/current 18 mask 0x04 |
| 72 | 072 | hfm_dtc | Сигнал скорости автомобиля: не обнаружен. | Vehicle-speed signal: not detected. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos68: stored 08/current 18 mask 0x08 |
| 73 | 073 | hfm_dtc | Сигнал скорости автомобиля: неправдоподобно высокий. | Vehicle-speed signal: implausibly high. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos69: stored 08/current 18 mask 0x10 |
| 74 | 074 | hfm_dtc | Реле PSV K3/1: замыкание на плюс. | PSV relay K3/1: short to positive. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos70: stored 08/current 18 mask 0x20 |
| 75 | 075 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | pos71: stored 08/current 18 mask 0x40 |
| 76 | 076 | hfm_dtc | Реле топливного насоса: обрыв или короткое замыкание. | Fuel-pump relay: open or short circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos72: stored 08/current 18 mask 0x80 |
| 77 | 077 | hfm_dtc | CO-потенциометр: замыкание на плюс. | CO potentiometer: short to positive. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos74: stored 09/current 19 mask 0x02 |
| 78 | 078 | hfm_dtc | CO-потенциометр: перемежающийся контакт. | CO potentiometer: intermittent connection. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos126: stored 0F/current 1F mask 0x20 |
| 79 | 079 | hfm_dtc | Датчик детонации 1: обрыв цепи. | Knock sensor 1: open circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos75: stored 09/current 19 mask 0x04 |
| 80 | 080 | hfm_dtc | Датчик детонации 2: обрыв цепи. | Knock sensor 2: open circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos76: stored 09/current 19 mask 0x08 |
| 81 | 081 | hfm_dtc | Достигнут предел смещения зажигания в позднюю сторону на одном или нескольких цилиндрах. | Ignition retard limit reached on one or more cylinders. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos77: stored 09/current 19 mask 0x10 |
| 82 | 082 | hfm_dtc | Разница углов зажигания между цилиндрами превышает 6° ПКВ. | Ignition-angle difference between cylinders exceeds 6 crankshaft degrees. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos78: stored 09/current 19 mask 0x20 |
| 83 | 083 | hfm_dtc | ЭБУ двигателя: неисправность схемы обработки детонации. | Engine ECU: knock-evaluation circuit fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos79: stored 09/current 19 mask 0x40 |
| 84 | 084 | hfm_dtc | Кратковременная ошибка адаптации смеси на ХХ или частичной нагрузке. | Transient mixture-adaptation fault at idle or partial load. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos80: stored 09/current 19 mask 0x80 |
| 85 | 085 | hfm_dtc | Цепь клапана/муфты/реле насоса вторичного воздуха: обрыв или КЗ. | Secondary-air pump valve/clutch/relay circuit: open or short circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos81: stored 0A/current 1A mask 0x01 |
| 86 | 086 | hfm_dtc | Клапан продувки адсорбера: обрыв или КЗ. | Purge valve: open or short circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos84: stored 0A/current 1A mask 0x08 |
| 87 | 087 | hfm_dtc | Клапан продувки адсорбера: замыкание на плюс АКБ. | Purge valve: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos85: stored 0A/current 1A mask 0x10 |
| 88 | 088 | hfm_dtc | Клапан задержки переключения передач вверх: обрыв или КЗ. | Upshift-delay valve: open or short circuit. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos86: stored 0A/current 1A mask 0x20 |
| 89 | 089 | hfm_dtc | Электромагнит изменения фаз распредвала: замыкание на плюс АКБ. | Camshaft timing solenoid: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos89: stored 0B/current 1B mask 0x01 |
| 90 | 090 | hfm_dtc | Электромагнит изменения фаз распредвала: обрыв или замыкание на массу. | Camshaft timing solenoid: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos90: stored 0B/current 1B mask 0x02 |
| 91 | 091 | hfm_dtc | Управление клапаном EGR: замыкание на плюс АКБ. | EGR valve control: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos91: stored 0B/current 1B mask 0x04 |
| 92 | 092 | hfm_dtc | Управление клапаном EGR: обрыв или замыкание на массу. | EGR valve control: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos92: stored 0B/current 1B mask 0x08 |
| 93 | 093 | hfm_dtc | Выключатель перегрузки трансмиссии: замыкание на массу. | Transmission overload switch: short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos93: stored 0B/current 1B mask 0x10 |
| 94 | 094 | hfm_dtc | Выключатель перегрузки трансмиссии замкнут при распознанной 2-й передаче. | Transmission overload switch closed while second gear is detected. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos94: stored 0B/current 1B mask 0x20 |
| 95 | 095 | hfm_dtc | Выключатель перегрузки трансмиссии разомкнут при распознанной 2-й передаче. | Transmission overload switch open while second gear is detected. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos95: stored 0B/current 1B mask 0x40 |
| 96 | 096 | hfm_dtc | Выключатель перегрузки трансмиссии: неправдоподобное состояние. | Transmission overload switch: implausible state. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos96: stored 0B/current 1B mask 0x80 |
| 97 | 097 | hfm_dtc | Ошибка CAN-связи со стороны ЭБУ двигателя. | CAN communication fault from the engine ECU. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos97: stored 0C/current 1C mask 0x01 |
| 98 | 098 | hfm_dtc | Ошибка CAN-связи со стороны блока ASR. | CAN communication fault from the ASR controller. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos98: stored 0C/current 1C mask 0x02 |
| 99 | 099 | hfm_dtc | Ошибка CAN-связи со стороны блока педали/круиза/ХХ. | CAN communication fault from the accelerator/cruise/idle controller. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos101: stored 0C/current 1C mask 0x10 |
| 100 | 100 | hfm_dtc | Ошибка CAN-связи со стороны диагностического модуля OBD-II. | CAN communication fault from the OBD-II diagnostic module. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos102: stored 0C/current 1C mask 0x20 |
| 101 | 101 | hfm_dtc | Сигнал стартера на клемме 50 отсутствует. | Starter signal at terminal 50: missing. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos105: stored 0D/current 1D mask 0x01 |
| 102 | 102 | hfm_dtc | Термопара катализатора: температура слишком высокая. | Catalyst thermocouple: temperature too high. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos106: stored 0D/current 1D mask 0x02 |
| 103 | 103 | hfm_dtc | Термопара катализатора: температура слишком низкая. | Catalyst thermocouple: temperature too low. | reference_supported | HFM-SFI: M111 2.2/2.3 L and M104 2.8/3.2 L, 1994-97; equipment/market dependent | pos107: stored 0D/current 1D mask 0x04 |
| 104 | 104 | hfm_dtc | Активно аварийное отключение топлива системой педали/круиза. | Accelerator/cruise-control fuel safety shutoff is active. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos108: stored 0D/current 1D mask 0x08 |
| 105 | 105 | hfm_dtc | Клапан резонансного впуска: замыкание на плюс АКБ. | Resonance-intake valve: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos110: stored 0D/current 1D mask 0x20 |
| 106 | 106 | hfm_dtc | Клапан резонансного впуска: обрыв или замыкание на массу. | Resonance-intake valve: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos111: stored 0D/current 1D mask 0x40 |
| 107 | 107 | hfm_dtc | Неисправность управления зажиганием (детализация не подтверждена). | Ignition control fault. Detailed condition not yet verified. | reconciliation_required | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos112: stored 0D/current 1D mask 0x80 |
| 108 | 108 | hfm_dtc | Реле подогрева нижнего лямбда-зонда: замыкание на плюс АКБ. | Downstream oxygen-heater relay: short to battery positive. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos114: stored 0E/current 1E mask 0x02 |
| 109 | 109 | hfm_dtc | Реле подогрева нижнего лямбда-зонда: обрыв или замыкание на массу. | Downstream oxygen-heater relay: open circuit or short to ground. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos115: stored 0E/current 1E mask 0x04 |
| 110 | 110 | hfm_dtc | Питание ЭБУ на клемме 87U: неправдоподобное напряжение. | Engine ECU supply at terminal 87U: implausible voltage. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos118: stored 0E/current 1E mask 0x20 |
| 111 | 111 | hfm_dtc | Питание ЭБУ на клемме 87U: пониженное напряжение. | Engine ECU supply at terminal 87U: undervoltage. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos119: stored 0E/current 1E mask 0x40 |
| 112 | 112 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos120: stored 0E/current 1E mask 0x80 |
| 113 | 113 | hfm_dtc | ЭБУ двигателя не закодирован. | Engine ECU has not been coded. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos11: stored 01/current 11 mask 0x04 |
| 114 | 114 | hfm_dtc | Идентификация ЭБУ двигателя недействительна. | Engine ECU identification: invalid. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos113: stored 0E/current 1E mask 0x01 |
| 115 | 115 | hfm_dtc | Байты кодировки ЭБУ двигателя недействительны. | Engine ECU coding bytes: invalid. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos116: stored 0E/current 1E mask 0x08 |
| 116 | 116 | hfm_dtc | Ошибка CAN-связи со стороны блока дистанционного центрального замка. | CAN communication fault from the remote central-locking controller. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos99: stored 0C/current 1C mask 0x04 |
| 117 | 117 | hfm_dtc | Условие пуска/дистанционной блокировки (детализация не подтверждена). | Starting/remote-locking condition. Detailed condition not yet verified. | reconciliation_required | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos104: stored 0C/current 1C mask 0x80 |
| 118 | 118 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | pos9: stored 01/current 11 mask 0x01 |
| 119 | 119 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | pos73: stored 09/current 19 mask 0x01 |
| 120 | 120 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 123 | 123 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | pos87: stored 0A/current 1A mask 0x40 |
| 124 | 124 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | pos88: stored 0A/current 1A mask 0x80 |
| 125 | 125 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos109: stored 0D/current 1D mask 0x10 |
| 126 | 126 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos117: stored 0E/current 1E mask 0x10 |
| 127 | 127 | hfm_dtc | Перепутаны исполнительные механизмы ХХ и педали/круиза. | Idle and cruise/idle actuators are interchanged. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | pos128: stored 0F/current 1F mask 0x80 |
| 128 | 128 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 129 | 129 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 130 | 130 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | not reachable in default mapping |
| 131 | 131 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 132 | 132 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 133 | 133 | hfm_dtc | Неисправность исполнительного механизма педали/круиза/ХХ. | Accelerator/cruise/idle actuator fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 134 | 134 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 135 | 135 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | not reachable in default mapping |
| 136 | 136 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | not reachable in default mapping |
| 137 | 137 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 138 | 138 | hfm_dtc | Неисправность исполнительного механизма педали/круиза/ХХ. | Accelerator/cruise/idle actuator fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 139 | 139 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | not reachable in default mapping |
| 140 | 140 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 141 | 141 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 142 | 142 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 143 | 143 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | not reachable in default mapping |
| 144 | 144 | hfm_dtc | Неисправность ЭБУ двигателя. | Engine ECU fault. | oem_supported | M104 HFM-SFI; OEM manual 09/00; equipment/version dependent | not reachable in default mapping |
| 145 | 145 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 146 | 146 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 147 | 147 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 148 | 148 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 149 | 149 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 150 | 150 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 151 | 151 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 152 | 152 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 153 | 153 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 154 | 154 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 155 | 155 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 156 | 156 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 157 | 157 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 158 | 158 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 159 | 159 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 160 | 160 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 161 | 161 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 162 | 162 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 163 | 163 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |
| 164 | 164 | hfm_dtc | Определение не подтверждено. | Definition not yet verified. | unverified | ECU applicability not yet verified. | only via variant remap |

## 13. Все битовые карты HHT-WIN (варианты V/B и цилиндры)

Найдено **7 таблиц**, они непосредственно находятся в `HFMWIN.DLL` по адресу функции `1020:1532`, DS сегмент 9. Выбор зависит от варианта `V/B`, количества цилиндров и диагностического индекса (символьные границы `I`, `P` — в терминах восстановленного кода).
### 13.1 C86E — 4-cyl, diag ident < 'I' (V and B)

| Байт | Бит | DTC № | Запрос для блока 0 | Запрос для блока 1 |
| --- | --- | --- | --- | --- |
| 0 | 0 | 2 | F1 00 | F1 10 |
| 0 | 1 | 3 | F1 00 | F1 10 |
| 0 | 2 | 4 | F1 00 | F1 10 |
| 0 | 3 | 6 | F1 00 | F1 10 |
| 0 | 4 | 7 | F1 00 | F1 10 |
| 0 | 5 | 9 | F1 00 | F1 10 |
| 0 | 6 | 10 | F1 00 | F1 10 |
| 0 | 7 | 11 | F1 00 | F1 10 |
| 1 | 1 | 12 | F1 01 | F1 11 |
| 1 | 2 | 113 | F1 01 | F1 11 |
| 1 | 3 | 14 | F1 01 | F1 11 |
| 1 | 4 | 15 | F1 01 | F1 11 |
| 1 | 5 | 17 | F1 01 | F1 11 |
| 1 | 6 | 18 | F1 01 | F1 11 |
| 1 | 7 | 20 | F1 01 | F1 11 |
| 2 | 0 | 21 | F1 02 | F1 12 |
| 2 | 1 | 22 | F1 02 | F1 12 |
| 2 | 2 | 23 | F1 02 | F1 12 |
| 2 | 3 | 24 | F1 02 | F1 12 |
| 2 | 4 | 25 | F1 02 | F1 12 |
| 3 | 0 | 29 | F1 03 | F1 13 |
| 3 | 1 | 30 | F1 03 | F1 13 |
| 3 | 2 | 31 | F1 03 | F1 13 |
| 3 | 6 | 35 | F1 03 | F1 13 |
| 3 | 7 | 36 | F1 03 | F1 13 |
| 4 | 0 | 37 | F1 04 | F1 14 |
| 4 | 1 | 38 | F1 04 | F1 14 |
| 4 | 2 | 39 | F1 04 | F1 14 |
| 4 | 3 | 40 | F1 04 | F1 14 |
| 4 | 4 | 41 | F1 04 | F1 14 |
| 4 | 5 | 42 | F1 04 | F1 14 |
| 4 | 6 | 43 | F1 04 | F1 14 |
| 4 | 7 | 44 | F1 04 | F1 14 |
| 5 | 4 | 49 | F1 05 | F1 15 |
| 5 | 5 | 50 | F1 05 | F1 15 |
| 5 | 6 | 51 | F1 05 | F1 15 |
| 5 | 7 | 52 | F1 05 | F1 15 |
| 6 | 0 | 53 | F1 06 | F1 16 |
| 6 | 1 | 54 | F1 06 | F1 16 |
| 6 | 2 | 55 | F1 06 | F1 16 |
| 6 | 3 | 56 | F1 06 | F1 16 |
| 6 | 4 | 57 | F1 06 | F1 16 |
| 6 | 5 | 58 | F1 06 | F1 16 |
| 6 | 6 | 59 | F1 06 | F1 16 |
| 6 | 7 | 60 | F1 06 | F1 16 |
| 7 | 3 | 64 | F1 07 | F1 17 |
| 7 | 4 | 65 | F1 07 | F1 17 |
| 7 | 5 | 66 | F1 07 | F1 17 |
| 7 | 6 | 67 | F1 07 | F1 17 |
| 7 | 7 | 68 | F1 07 | F1 17 |
| 8 | 0 | 69 | F1 08 | F1 18 |
| 8 | 1 | 70 | F1 08 | F1 18 |
| 8 | 2 | 71 | F1 08 | F1 18 |
| 8 | 3 | 72 | F1 08 | F1 18 |
| 8 | 4 | 73 | F1 08 | F1 18 |
| 8 | 5 | 74 | F1 08 | F1 18 |
| 8 | 6 | 75 | F1 08 | F1 18 |
| 8 | 7 | 76 | F1 08 | F1 18 |
| 9 | 1 | 77 | F1 09 | F1 19 |
| 9 | 2 | 79 | F1 09 | F1 19 |
| 9 | 3 | 80 | F1 09 | F1 19 |
| 9 | 4 | 81 | F1 09 | F1 19 |
| 9 | 5 | 82 | F1 09 | F1 19 |
| 9 | 6 | 83 | F1 09 | F1 19 |
| 9 | 7 | 84 | F1 09 | F1 19 |
| 10 | 0 | 85 | F1 0A | F1 1A |
| 10 | 3 | 86 | F1 0A | F1 1A |
| 10 | 4 | 87 | F1 0A | F1 1A |
| 10 | 5 | 88 | F1 0A | F1 1A |
| 11 | 0 | 89 | F1 0B | F1 1B |
| 11 | 1 | 90 | F1 0B | F1 1B |
| 11 | 2 | 91 | F1 0B | F1 1B |
| 11 | 3 | 92 | F1 0B | F1 1B |
| 11 | 4 | 93 | F1 0B | F1 1B |
| 11 | 5 | 94 | F1 0B | F1 1B |
| 11 | 6 | 95 | F1 0B | F1 1B |
| 11 | 7 | 96 | F1 0B | F1 1B |
| 12 | 0 | 97 | F1 0C | F1 1C |
| 12 | 1 | 98 | F1 0C | F1 1C |
| 12 | 2 | 116 | F1 0C | F1 1C |
| 12 | 4 | 99 | F1 0C | F1 1C |
| 12 | 5 | 100 | F1 0C | F1 1C |
| 12 | 7 | 117 | F1 0C | F1 1C |
| 13 | 0 | 101 | F1 0D | F1 1D |
| 13 | 1 | 102 | F1 0D | F1 1D |
| 13 | 2 | 103 | F1 0D | F1 1D |
| 13 | 3 | 104 | F1 0D | F1 1D |
| 13 | 5 | 105 | F1 0D | F1 1D |
| 13 | 6 | 106 | F1 0D | F1 1D |
| 13 | 7 | 107 | F1 0D | F1 1D |
| 14 | 0 | 114 | F1 0E | F1 1E |
| 14 | 1 | 108 | F1 0E | F1 1E |
| 14 | 2 | 109 | F1 0E | F1 1E |
| 14 | 3 | 115 | F1 0E | F1 1E |
| 14 | 5 | 110 | F1 0E | F1 1E |
| 14 | 6 | 111 | F1 0E | F1 1E |
| 14 | 7 | 112 | F1 0E | F1 1E |
| 15 | 0 | 5 | F1 0F | F1 1F |
| 15 | 1 | 8 | F1 0F | F1 1F |
| 15 | 2 | 13 | F1 0F | F1 1F |
| 15 | 3 | 16 | F1 0F | F1 1F |
| 15 | 4 | 19 | F1 0F | F1 1F |
| 15 | 5 | 78 | F1 0F | F1 1F |

### 13.2 C8EE — 6-cyl, diag ident < 'I' (V and B)

| Байт | Бит | DTC № | Запрос для блока 0 | Запрос для блока 1 |
| --- | --- | --- | --- | --- |
| 0 | 0 | 2 | F1 00 | F1 10 |
| 0 | 1 | 3 | F1 00 | F1 10 |
| 0 | 2 | 4 | F1 00 | F1 10 |
| 0 | 3 | 6 | F1 00 | F1 10 |
| 0 | 4 | 7 | F1 00 | F1 10 |
| 0 | 5 | 9 | F1 00 | F1 10 |
| 0 | 6 | 10 | F1 00 | F1 10 |
| 0 | 7 | 11 | F1 00 | F1 10 |
| 1 | 1 | 12 | F1 01 | F1 11 |
| 1 | 2 | 113 | F1 01 | F1 11 |
| 1 | 3 | 14 | F1 01 | F1 11 |
| 1 | 4 | 15 | F1 01 | F1 11 |
| 1 | 5 | 17 | F1 01 | F1 11 |
| 1 | 6 | 18 | F1 01 | F1 11 |
| 1 | 7 | 20 | F1 01 | F1 11 |
| 2 | 0 | 21 | F1 02 | F1 12 |
| 2 | 1 | 22 | F1 02 | F1 12 |
| 2 | 2 | 23 | F1 02 | F1 12 |
| 2 | 3 | 24 | F1 02 | F1 12 |
| 2 | 4 | 25 | F1 02 | F1 12 |
| 2 | 5 | 26 | F1 02 | F1 12 |
| 2 | 6 | 27 | F1 02 | F1 12 |
| 2 | 7 | 28 | F1 02 | F1 12 |
| 3 | 0 | 29 | F1 03 | F1 13 |
| 3 | 1 | 30 | F1 03 | F1 13 |
| 3 | 2 | 31 | F1 03 | F1 13 |
| 3 | 3 | 32 | F1 03 | F1 13 |
| 3 | 4 | 33 | F1 03 | F1 13 |
| 3 | 5 | 34 | F1 03 | F1 13 |
| 3 | 6 | 35 | F1 03 | F1 13 |
| 3 | 7 | 36 | F1 03 | F1 13 |
| 4 | 0 | 37 | F1 04 | F1 14 |
| 4 | 1 | 38 | F1 04 | F1 14 |
| 4 | 2 | 39 | F1 04 | F1 14 |
| 4 | 3 | 40 | F1 04 | F1 14 |
| 4 | 4 | 41 | F1 04 | F1 14 |
| 4 | 5 | 42 | F1 04 | F1 14 |
| 4 | 6 | 43 | F1 04 | F1 14 |
| 4 | 7 | 44 | F1 04 | F1 14 |
| 5 | 0 | 45 | F1 05 | F1 15 |
| 5 | 1 | 46 | F1 05 | F1 15 |
| 5 | 2 | 47 | F1 05 | F1 15 |
| 5 | 3 | 48 | F1 05 | F1 15 |
| 5 | 4 | 49 | F1 05 | F1 15 |
| 5 | 5 | 50 | F1 05 | F1 15 |
| 5 | 6 | 51 | F1 05 | F1 15 |
| 5 | 7 | 52 | F1 05 | F1 15 |
| 6 | 0 | 53 | F1 06 | F1 16 |
| 6 | 1 | 54 | F1 06 | F1 16 |
| 6 | 2 | 61 | F1 06 | F1 16 |
| 6 | 3 | 62 | F1 06 | F1 16 |
| 6 | 4 | 63 | F1 06 | F1 16 |
| 6 | 5 | 55 | F1 06 | F1 16 |
| 6 | 6 | 56 | F1 06 | F1 16 |
| 6 | 7 | 57 | F1 06 | F1 16 |
| 7 | 0 | 58 | F1 07 | F1 17 |
| 7 | 1 | 59 | F1 07 | F1 17 |
| 7 | 2 | 60 | F1 07 | F1 17 |
| 7 | 3 | 64 | F1 07 | F1 17 |
| 7 | 4 | 65 | F1 07 | F1 17 |
| 7 | 5 | 66 | F1 07 | F1 17 |
| 7 | 6 | 67 | F1 07 | F1 17 |
| 7 | 7 | 68 | F1 07 | F1 17 |
| 8 | 0 | 69 | F1 08 | F1 18 |
| 8 | 1 | 70 | F1 08 | F1 18 |
| 8 | 2 | 71 | F1 08 | F1 18 |
| 8 | 3 | 72 | F1 08 | F1 18 |
| 8 | 4 | 73 | F1 08 | F1 18 |
| 8 | 5 | 74 | F1 08 | F1 18 |
| 8 | 7 | 76 | F1 08 | F1 18 |
| 9 | 1 | 77 | F1 09 | F1 19 |
| 9 | 2 | 79 | F1 09 | F1 19 |
| 9 | 3 | 80 | F1 09 | F1 19 |
| 9 | 4 | 81 | F1 09 | F1 19 |
| 9 | 5 | 82 | F1 09 | F1 19 |
| 9 | 6 | 83 | F1 09 | F1 19 |
| 9 | 7 | 84 | F1 09 | F1 19 |
| 10 | 0 | 85 | F1 0A | F1 1A |
| 10 | 3 | 86 | F1 0A | F1 1A |
| 10 | 4 | 87 | F1 0A | F1 1A |
| 10 | 5 | 88 | F1 0A | F1 1A |
| 11 | 0 | 89 | F1 0B | F1 1B |
| 11 | 1 | 90 | F1 0B | F1 1B |
| 11 | 2 | 91 | F1 0B | F1 1B |
| 11 | 3 | 92 | F1 0B | F1 1B |
| 11 | 4 | 93 | F1 0B | F1 1B |
| 11 | 5 | 94 | F1 0B | F1 1B |
| 11 | 6 | 95 | F1 0B | F1 1B |
| 11 | 7 | 96 | F1 0B | F1 1B |
| 12 | 0 | 97 | F1 0C | F1 1C |
| 12 | 1 | 98 | F1 0C | F1 1C |
| 12 | 2 | 116 | F1 0C | F1 1C |
| 12 | 4 | 99 | F1 0C | F1 1C |
| 12 | 5 | 100 | F1 0C | F1 1C |
| 12 | 7 | 117 | F1 0C | F1 1C |
| 13 | 0 | 101 | F1 0D | F1 1D |
| 13 | 1 | 102 | F1 0D | F1 1D |
| 13 | 2 | 103 | F1 0D | F1 1D |
| 13 | 3 | 104 | F1 0D | F1 1D |
| 13 | 5 | 105 | F1 0D | F1 1D |
| 13 | 6 | 106 | F1 0D | F1 1D |
| 13 | 7 | 107 | F1 0D | F1 1D |
| 14 | 0 | 114 | F1 0E | F1 1E |
| 14 | 1 | 108 | F1 0E | F1 1E |
| 14 | 2 | 109 | F1 0E | F1 1E |
| 14 | 3 | 115 | F1 0E | F1 1E |
| 14 | 5 | 110 | F1 0E | F1 1E |
| 14 | 6 | 111 | F1 0E | F1 1E |
| 14 | 7 | 112 | F1 0E | F1 1E |
| 15 | 0 | 5 | F1 0F | F1 1F |
| 15 | 1 | 8 | F1 0F | F1 1F |
| 15 | 2 | 13 | F1 0F | F1 1F |
| 15 | 3 | 16 | F1 0F | F1 1F |
| 15 | 4 | 19 | F1 0F | F1 1F |
| 15 | 5 | 78 | F1 0F | F1 1F |

### 13.3 C96E — 4-cyl, V, 'I' <= diag < 'P'

| Байт | Бит | DTC № | Запрос для блока 0 | Запрос для блока 1 |
| --- | --- | --- | --- | --- |
| 0 | 0 | 2 | F1 00 | F1 10 |
| 0 | 1 | 3 | F1 00 | F1 10 |
| 0 | 2 | 4 | F1 00 | F1 10 |
| 0 | 3 | 6 | F1 00 | F1 10 |
| 0 | 4 | 7 | F1 00 | F1 10 |
| 0 | 5 | 9 | F1 00 | F1 10 |
| 0 | 6 | 10 | F1 00 | F1 10 |
| 0 | 7 | 11 | F1 00 | F1 10 |
| 1 | 0 | 118 | F1 01 | F1 11 |
| 1 | 1 | 12 | F1 01 | F1 11 |
| 1 | 2 | 113 | F1 01 | F1 11 |
| 1 | 3 | 14 | F1 01 | F1 11 |
| 1 | 4 | 15 | F1 01 | F1 11 |
| 1 | 5 | 17 | F1 01 | F1 11 |
| 1 | 6 | 18 | F1 01 | F1 11 |
| 1 | 7 | 20 | F1 01 | F1 11 |
| 2 | 0 | 21 | F1 02 | F1 12 |
| 2 | 1 | 22 | F1 02 | F1 12 |
| 2 | 2 | 23 | F1 02 | F1 12 |
| 2 | 3 | 24 | F1 02 | F1 12 |
| 2 | 4 | 25 | F1 02 | F1 12 |
| 3 | 0 | 29 | F1 03 | F1 13 |
| 3 | 1 | 30 | F1 03 | F1 13 |
| 3 | 2 | 31 | F1 03 | F1 13 |
| 3 | 6 | 35 | F1 03 | F1 13 |
| 3 | 7 | 36 | F1 03 | F1 13 |
| 4 | 0 | 37 | F1 04 | F1 14 |
| 4 | 1 | 38 | F1 04 | F1 14 |
| 4 | 2 | 39 | F1 04 | F1 14 |
| 4 | 3 | 40 | F1 04 | F1 14 |
| 4 | 4 | 41 | F1 04 | F1 14 |
| 4 | 5 | 42 | F1 04 | F1 14 |
| 4 | 6 | 43 | F1 04 | F1 14 |
| 4 | 7 | 44 | F1 04 | F1 14 |
| 5 | 4 | 49 | F1 05 | F1 15 |
| 5 | 5 | 50 | F1 05 | F1 15 |
| 5 | 6 | 51 | F1 05 | F1 15 |
| 5 | 7 | 52 | F1 05 | F1 15 |
| 6 | 0 | 53 | F1 06 | F1 16 |
| 6 | 1 | 54 | F1 06 | F1 16 |
| 6 | 2 | 55 | F1 06 | F1 16 |
| 6 | 3 | 56 | F1 06 | F1 16 |
| 6 | 4 | 57 | F1 06 | F1 16 |
| 6 | 5 | 58 | F1 06 | F1 16 |
| 6 | 6 | 59 | F1 06 | F1 16 |
| 6 | 7 | 60 | F1 06 | F1 16 |
| 7 | 3 | 64 | F1 07 | F1 17 |
| 7 | 4 | 65 | F1 07 | F1 17 |
| 7 | 5 | 66 | F1 07 | F1 17 |
| 7 | 6 | 67 | F1 07 | F1 17 |
| 7 | 7 | 68 | F1 07 | F1 17 |
| 8 | 0 | 69 | F1 08 | F1 18 |
| 8 | 1 | 70 | F1 08 | F1 18 |
| 8 | 2 | 71 | F1 08 | F1 18 |
| 8 | 3 | 72 | F1 08 | F1 18 |
| 8 | 4 | 73 | F1 08 | F1 18 |
| 8 | 5 | 122 | F1 08 | F1 18 |
| 8 | 6 | 121 | F1 08 | F1 18 |
| 8 | 7 | 76 | F1 08 | F1 18 |
| 9 | 0 | 119 | F1 09 | F1 19 |
| 9 | 1 | 120 | F1 09 | F1 19 |
| 9 | 2 | 79 | F1 09 | F1 19 |
| 9 | 3 | 80 | F1 09 | F1 19 |
| 9 | 4 | 81 | F1 09 | F1 19 |
| 9 | 5 | 82 | F1 09 | F1 19 |
| 9 | 7 | 84 | F1 09 | F1 19 |
| 10 | 3 | 86 | F1 0A | F1 1A |
| 10 | 4 | 87 | F1 0A | F1 1A |
| 10 | 5 | 88 | F1 0A | F1 1A |
| 10 | 6 | 123 | F1 0A | F1 1A |
| 10 | 7 | 124 | F1 0A | F1 1A |
| 11 | 0 | 89 | F1 0B | F1 1B |
| 11 | 1 | 90 | F1 0B | F1 1B |
| 11 | 2 | 91 | F1 0B | F1 1B |
| 11 | 3 | 92 | F1 0B | F1 1B |
| 11 | 4 | 93 | F1 0B | F1 1B |
| 11 | 5 | 94 | F1 0B | F1 1B |
| 11 | 6 | 95 | F1 0B | F1 1B |
| 11 | 7 | 96 | F1 0B | F1 1B |
| 12 | 0 | 97 | F1 0C | F1 1C |
| 12 | 1 | 98 | F1 0C | F1 1C |
| 12 | 2 | 116 | F1 0C | F1 1C |
| 12 | 4 | 99 | F1 0C | F1 1C |
| 12 | 5 | 100 | F1 0C | F1 1C |
| 12 | 7 | 117 | F1 0C | F1 1C |
| 13 | 0 | 101 | F1 0D | F1 1D |
| 13 | 1 | 102 | F1 0D | F1 1D |
| 13 | 2 | 103 | F1 0D | F1 1D |
| 13 | 3 | 104 | F1 0D | F1 1D |
| 13 | 4 | 125 | F1 0D | F1 1D |
| 13 | 5 | 105 | F1 0D | F1 1D |
| 13 | 6 | 106 | F1 0D | F1 1D |
| 13 | 7 | 107 | F1 0D | F1 1D |
| 14 | 0 | 114 | F1 0E | F1 1E |
| 14 | 1 | 108 | F1 0E | F1 1E |
| 14 | 2 | 109 | F1 0E | F1 1E |
| 14 | 3 | 115 | F1 0E | F1 1E |
| 14 | 4 | 126 | F1 0E | F1 1E |
| 14 | 5 | 110 | F1 0E | F1 1E |
| 14 | 6 | 111 | F1 0E | F1 1E |
| 14 | 7 | 112 | F1 0E | F1 1E |
| 15 | 0 | 5 | F1 0F | F1 1F |
| 15 | 1 | 8 | F1 0F | F1 1F |
| 15 | 2 | 13 | F1 0F | F1 1F |
| 15 | 3 | 16 | F1 0F | F1 1F |
| 15 | 4 | 19 | F1 0F | F1 1F |
| 15 | 5 | 78 | F1 0F | F1 1F |
| 15 | 7 | 127 | F1 0F | F1 1F |
| 16 | 0 | 128 | F1 30 | F1 40 |
| 16 | 1 | 129 | F1 30 | F1 40 |
| 16 | 2 | 130 | F1 30 | F1 40 |
| 16 | 3 | 131 | F1 30 | F1 40 |
| 16 | 4 | 132 | F1 30 | F1 40 |
| 17 | 0 | 133 | F1 31 | F1 41 |
| 17 | 1 | 134 | F1 31 | F1 41 |
| 17 | 2 | 135 | F1 31 | F1 41 |
| 17 | 4 | 136 | F1 31 | F1 41 |
| 17 | 6 | 137 | F1 31 | F1 41 |
| 17 | 7 | 138 | F1 31 | F1 41 |
| 18 | 0 | 139 | F1 32 | F1 42 |
| 18 | 1 | 140 | F1 32 | F1 42 |
| 18 | 2 | 141 | F1 32 | F1 42 |
| 18 | 3 | 142 | F1 32 | F1 42 |
| 18 | 4 | 143 | F1 32 | F1 42 |
| 18 | 5 | 144 | F1 32 | F1 42 |

### 13.4 CA06 — 6-cyl, V, diag >= 'I'

| Байт | Бит | DTC № | Запрос для блока 0 | Запрос для блока 1 |
| --- | --- | --- | --- | --- |
| 0 | 0 | 2 | F1 00 | F1 10 |
| 0 | 1 | 3 | F1 00 | F1 10 |
| 0 | 2 | 4 | F1 00 | F1 10 |
| 0 | 3 | 6 | F1 00 | F1 10 |
| 0 | 4 | 7 | F1 00 | F1 10 |
| 0 | 5 | 9 | F1 00 | F1 10 |
| 0 | 6 | 10 | F1 00 | F1 10 |
| 0 | 7 | 11 | F1 00 | F1 10 |
| 1 | 1 | 12 | F1 01 | F1 11 |
| 1 | 2 | 113 | F1 01 | F1 11 |
| 1 | 3 | 14 | F1 01 | F1 11 |
| 1 | 4 | 15 | F1 01 | F1 11 |
| 1 | 5 | 17 | F1 01 | F1 11 |
| 1 | 6 | 18 | F1 01 | F1 11 |
| 1 | 7 | 20 | F1 01 | F1 11 |
| 2 | 0 | 21 | F1 02 | F1 12 |
| 2 | 1 | 22 | F1 02 | F1 12 |
| 2 | 2 | 23 | F1 02 | F1 12 |
| 2 | 3 | 24 | F1 02 | F1 12 |
| 2 | 4 | 25 | F1 02 | F1 12 |
| 2 | 5 | 26 | F1 02 | F1 12 |
| 2 | 6 | 27 | F1 02 | F1 12 |
| 2 | 7 | 28 | F1 02 | F1 12 |
| 3 | 0 | 29 | F1 03 | F1 13 |
| 3 | 1 | 30 | F1 03 | F1 13 |
| 3 | 2 | 31 | F1 03 | F1 13 |
| 3 | 3 | 32 | F1 03 | F1 13 |
| 3 | 4 | 33 | F1 03 | F1 13 |
| 3 | 5 | 34 | F1 03 | F1 13 |
| 3 | 6 | 35 | F1 03 | F1 13 |
| 3 | 7 | 36 | F1 03 | F1 13 |
| 4 | 0 | 37 | F1 04 | F1 14 |
| 4 | 1 | 38 | F1 04 | F1 14 |
| 4 | 2 | 39 | F1 04 | F1 14 |
| 4 | 3 | 40 | F1 04 | F1 14 |
| 4 | 4 | 41 | F1 04 | F1 14 |
| 4 | 5 | 42 | F1 04 | F1 14 |
| 4 | 6 | 43 | F1 04 | F1 14 |
| 4 | 7 | 44 | F1 04 | F1 14 |
| 5 | 0 | 45 | F1 05 | F1 15 |
| 5 | 1 | 46 | F1 05 | F1 15 |
| 5 | 2 | 47 | F1 05 | F1 15 |
| 5 | 3 | 48 | F1 05 | F1 15 |
| 5 | 4 | 49 | F1 05 | F1 15 |
| 5 | 5 | 50 | F1 05 | F1 15 |
| 5 | 6 | 51 | F1 05 | F1 15 |
| 5 | 7 | 52 | F1 05 | F1 15 |
| 6 | 0 | 53 | F1 06 | F1 16 |
| 6 | 1 | 54 | F1 06 | F1 16 |
| 6 | 2 | 61 | F1 06 | F1 16 |
| 6 | 3 | 62 | F1 06 | F1 16 |
| 6 | 4 | 63 | F1 06 | F1 16 |
| 6 | 5 | 55 | F1 06 | F1 16 |
| 6 | 6 | 56 | F1 06 | F1 16 |
| 6 | 7 | 57 | F1 06 | F1 16 |
| 7 | 0 | 58 | F1 07 | F1 17 |
| 7 | 1 | 59 | F1 07 | F1 17 |
| 7 | 2 | 60 | F1 07 | F1 17 |
| 7 | 3 | 64 | F1 07 | F1 17 |
| 7 | 4 | 65 | F1 07 | F1 17 |
| 7 | 5 | 66 | F1 07 | F1 17 |
| 7 | 6 | 67 | F1 07 | F1 17 |
| 7 | 7 | 68 | F1 07 | F1 17 |
| 8 | 0 | 69 | F1 08 | F1 18 |
| 8 | 1 | 70 | F1 08 | F1 18 |
| 8 | 2 | 71 | F1 08 | F1 18 |
| 8 | 3 | 72 | F1 08 | F1 18 |
| 8 | 4 | 73 | F1 08 | F1 18 |
| 8 | 5 | 122 | F1 08 | F1 18 |
| 8 | 6 | 121 | F1 08 | F1 18 |
| 8 | 7 | 76 | F1 08 | F1 18 |
| 9 | 1 | 120 | F1 09 | F1 19 |
| 9 | 2 | 79 | F1 09 | F1 19 |
| 9 | 3 | 80 | F1 09 | F1 19 |
| 9 | 4 | 81 | F1 09 | F1 19 |
| 9 | 5 | 82 | F1 09 | F1 19 |
| 9 | 6 | 83 | F1 09 | F1 19 |
| 9 | 7 | 84 | F1 09 | F1 19 |
| 10 | 0 | 85 | F1 0A | F1 1A |
| 10 | 3 | 86 | F1 0A | F1 1A |
| 10 | 4 | 87 | F1 0A | F1 1A |
| 10 | 5 | 88 | F1 0A | F1 1A |
| 11 | 0 | 89 | F1 0B | F1 1B |
| 11 | 1 | 90 | F1 0B | F1 1B |
| 11 | 2 | 91 | F1 0B | F1 1B |
| 11 | 3 | 92 | F1 0B | F1 1B |
| 11 | 4 | 93 | F1 0B | F1 1B |
| 11 | 5 | 94 | F1 0B | F1 1B |
| 11 | 6 | 95 | F1 0B | F1 1B |
| 11 | 7 | 96 | F1 0B | F1 1B |
| 12 | 0 | 97 | F1 0C | F1 1C |
| 12 | 1 | 98 | F1 0C | F1 1C |
| 12 | 2 | 116 | F1 0C | F1 1C |
| 12 | 4 | 99 | F1 0C | F1 1C |
| 12 | 5 | 100 | F1 0C | F1 1C |
| 12 | 7 | 117 | F1 0C | F1 1C |
| 13 | 0 | 101 | F1 0D | F1 1D |
| 13 | 1 | 102 | F1 0D | F1 1D |
| 13 | 2 | 103 | F1 0D | F1 1D |
| 13 | 3 | 104 | F1 0D | F1 1D |
| 13 | 4 | 125 | F1 0D | F1 1D |
| 13 | 5 | 105 | F1 0D | F1 1D |
| 13 | 6 | 106 | F1 0D | F1 1D |
| 13 | 7 | 107 | F1 0D | F1 1D |
| 14 | 0 | 114 | F1 0E | F1 1E |
| 14 | 1 | 108 | F1 0E | F1 1E |
| 14 | 2 | 109 | F1 0E | F1 1E |
| 14 | 3 | 115 | F1 0E | F1 1E |
| 14 | 4 | 126 | F1 0E | F1 1E |
| 14 | 5 | 110 | F1 0E | F1 1E |
| 14 | 6 | 111 | F1 0E | F1 1E |
| 14 | 7 | 112 | F1 0E | F1 1E |
| 15 | 0 | 5 | F1 0F | F1 1F |
| 15 | 1 | 8 | F1 0F | F1 1F |
| 15 | 2 | 13 | F1 0F | F1 1F |
| 15 | 3 | 16 | F1 0F | F1 1F |
| 15 | 4 | 19 | F1 0F | F1 1F |
| 15 | 5 | 78 | F1 0F | F1 1F |
| 15 | 7 | 127 | F1 0F | F1 1F |
| 16 | 0 | 128 | F1 30 | F1 40 |
| 16 | 1 | 129 | F1 30 | F1 40 |
| 16 | 2 | 130 | F1 30 | F1 40 |
| 16 | 3 | 131 | F1 30 | F1 40 |
| 16 | 4 | 132 | F1 30 | F1 40 |
| 17 | 0 | 133 | F1 31 | F1 41 |
| 17 | 1 | 134 | F1 31 | F1 41 |
| 17 | 2 | 135 | F1 31 | F1 41 |
| 17 | 4 | 136 | F1 31 | F1 41 |
| 17 | 6 | 137 | F1 31 | F1 41 |
| 17 | 7 | 138 | F1 31 | F1 41 |
| 18 | 0 | 139 | F1 32 | F1 42 |
| 18 | 1 | 140 | F1 32 | F1 42 |
| 18 | 2 | 141 | F1 32 | F1 42 |
| 18 | 3 | 142 | F1 32 | F1 42 |
| 18 | 4 | 143 | F1 32 | F1 42 |
| 18 | 5 | 144 | F1 32 | F1 42 |

### 13.5 CA9E — 4-cyl, B, diag >= 'I'

| Байт | Бит | DTC № | Запрос для блока 0 | Запрос для блока 1 |
| --- | --- | --- | --- | --- |
| 0 | 0 | 2 | F1 00 | F1 10 |
| 0 | 1 | 3 | F1 00 | F1 10 |
| 0 | 2 | 4 | F1 00 | F1 10 |
| 0 | 3 | 6 | F1 00 | F1 10 |
| 0 | 4 | 7 | F1 00 | F1 10 |
| 0 | 5 | 9 | F1 00 | F1 10 |
| 0 | 6 | 10 | F1 00 | F1 10 |
| 0 | 7 | 11 | F1 00 | F1 10 |
| 1 | 0 | 118 | F1 01 | F1 11 |
| 1 | 1 | 12 | F1 01 | F1 11 |
| 1 | 2 | 113 | F1 01 | F1 11 |
| 1 | 3 | 14 | F1 01 | F1 11 |
| 1 | 4 | 15 | F1 01 | F1 11 |
| 1 | 5 | 17 | F1 01 | F1 11 |
| 1 | 6 | 18 | F1 01 | F1 11 |
| 1 | 7 | 20 | F1 01 | F1 11 |
| 2 | 0 | 21 | F1 02 | F1 12 |
| 2 | 1 | 22 | F1 02 | F1 12 |
| 2 | 2 | 23 | F1 02 | F1 12 |
| 2 | 3 | 24 | F1 02 | F1 12 |
| 2 | 4 | 25 | F1 02 | F1 12 |
| 3 | 0 | 29 | F1 03 | F1 13 |
| 3 | 1 | 30 | F1 03 | F1 13 |
| 3 | 2 | 31 | F1 03 | F1 13 |
| 3 | 6 | 35 | F1 03 | F1 13 |
| 3 | 7 | 36 | F1 03 | F1 13 |
| 4 | 0 | 37 | F1 04 | F1 14 |
| 4 | 1 | 38 | F1 04 | F1 14 |
| 4 | 2 | 41 | F1 04 | F1 14 |
| 4 | 3 | 42 | F1 04 | F1 14 |
| 4 | 4 | 43 | F1 04 | F1 14 |
| 4 | 5 | 44 | F1 04 | F1 14 |
| 4 | 6 | 39 | F1 04 | F1 14 |
| 4 | 7 | 40 | F1 04 | F1 14 |
| 5 | 4 | 49 | F1 05 | F1 15 |
| 5 | 5 | 50 | F1 05 | F1 15 |
| 5 | 6 | 51 | F1 05 | F1 15 |
| 5 | 7 | 52 | F1 05 | F1 15 |
| 6 | 0 | 53 | F1 06 | F1 16 |
| 6 | 1 | 54 | F1 06 | F1 16 |
| 6 | 2 | 55 | F1 06 | F1 16 |
| 6 | 3 | 56 | F1 06 | F1 16 |
| 6 | 4 | 57 | F1 06 | F1 16 |
| 6 | 5 | 58 | F1 06 | F1 16 |
| 6 | 6 | 59 | F1 06 | F1 16 |
| 6 | 7 | 60 | F1 06 | F1 16 |
| 7 | 3 | 64 | F1 07 | F1 17 |
| 7 | 4 | 65 | F1 07 | F1 17 |
| 7 | 5 | 66 | F1 07 | F1 17 |
| 7 | 6 | 67 | F1 07 | F1 17 |
| 7 | 7 | 68 | F1 07 | F1 17 |
| 8 | 0 | 69 | F1 08 | F1 18 |
| 8 | 1 | 70 | F1 08 | F1 18 |
| 8 | 2 | 71 | F1 08 | F1 18 |
| 8 | 3 | 72 | F1 08 | F1 18 |
| 8 | 4 | 73 | F1 08 | F1 18 |
| 8 | 5 | 122 | F1 08 | F1 18 |
| 8 | 6 | 121 | F1 08 | F1 18 |
| 8 | 7 | 76 | F1 08 | F1 18 |
| 9 | 0 | 119 | F1 09 | F1 19 |
| 9 | 1 | 120 | F1 09 | F1 19 |
| 9 | 2 | 79 | F1 09 | F1 19 |
| 9 | 3 | 80 | F1 09 | F1 19 |
| 9 | 4 | 81 | F1 09 | F1 19 |
| 9 | 5 | 82 | F1 09 | F1 19 |
| 9 | 7 | 84 | F1 09 | F1 19 |
| 10 | 3 | 86 | F1 0A | F1 1A |
| 10 | 4 | 87 | F1 0A | F1 1A |
| 10 | 5 | 88 | F1 0A | F1 1A |
| 10 | 6 | 123 | F1 0A | F1 1A |
| 10 | 7 | 124 | F1 0A | F1 1A |
| 11 | 0 | 89 | F1 0B | F1 1B |
| 11 | 1 | 90 | F1 0B | F1 1B |
| 11 | 2 | 91 | F1 0B | F1 1B |
| 11 | 3 | 92 | F1 0B | F1 1B |
| 11 | 4 | 93 | F1 0B | F1 1B |
| 11 | 5 | 94 | F1 0B | F1 1B |
| 11 | 6 | 95 | F1 0B | F1 1B |
| 11 | 7 | 96 | F1 0B | F1 1B |
| 12 | 0 | 97 | F1 0C | F1 1C |
| 12 | 1 | 98 | F1 0C | F1 1C |
| 12 | 2 | 116 | F1 0C | F1 1C |
| 12 | 4 | 99 | F1 0C | F1 1C |
| 12 | 5 | 100 | F1 0C | F1 1C |
| 12 | 7 | 117 | F1 0C | F1 1C |
| 13 | 0 | 101 | F1 0D | F1 1D |
| 13 | 1 | 102 | F1 0D | F1 1D |
| 13 | 2 | 103 | F1 0D | F1 1D |
| 13 | 3 | 104 | F1 0D | F1 1D |
| 13 | 4 | 125 | F1 0D | F1 1D |
| 13 | 5 | 105 | F1 0D | F1 1D |
| 13 | 6 | 106 | F1 0D | F1 1D |
| 13 | 7 | 107 | F1 0D | F1 1D |
| 14 | 0 | 114 | F1 0E | F1 1E |
| 14 | 1 | 108 | F1 0E | F1 1E |
| 14 | 2 | 109 | F1 0E | F1 1E |
| 14 | 3 | 115 | F1 0E | F1 1E |
| 14 | 4 | 126 | F1 0E | F1 1E |
| 14 | 5 | 110 | F1 0E | F1 1E |
| 14 | 6 | 111 | F1 0E | F1 1E |
| 14 | 7 | 112 | F1 0E | F1 1E |
| 15 | 0 | 5 | F1 0F | F1 1F |
| 15 | 1 | 8 | F1 0F | F1 1F |
| 15 | 2 | 13 | F1 0F | F1 1F |
| 15 | 3 | 16 | F1 0F | F1 1F |
| 15 | 4 | 19 | F1 0F | F1 1F |
| 15 | 5 | 78 | F1 0F | F1 1F |

### 13.6 CB1E — 6-cyl, B, diag >= 'I'

| Байт | Бит | DTC № | Запрос для блока 0 | Запрос для блока 1 |
| --- | --- | --- | --- | --- |
| 0 | 0 | 2 | F1 00 | F1 10 |
| 0 | 1 | 3 | F1 00 | F1 10 |
| 0 | 2 | 4 | F1 00 | F1 10 |
| 0 | 3 | 6 | F1 00 | F1 10 |
| 0 | 4 | 7 | F1 00 | F1 10 |
| 0 | 5 | 9 | F1 00 | F1 10 |
| 0 | 6 | 10 | F1 00 | F1 10 |
| 0 | 7 | 11 | F1 00 | F1 10 |
| 1 | 1 | 12 | F1 01 | F1 11 |
| 1 | 2 | 113 | F1 01 | F1 11 |
| 1 | 3 | 14 | F1 01 | F1 11 |
| 1 | 4 | 15 | F1 01 | F1 11 |
| 1 | 5 | 17 | F1 01 | F1 11 |
| 1 | 6 | 18 | F1 01 | F1 11 |
| 1 | 7 | 20 | F1 01 | F1 11 |
| 2 | 0 | 21 | F1 02 | F1 12 |
| 2 | 1 | 22 | F1 02 | F1 12 |
| 2 | 2 | 23 | F1 02 | F1 12 |
| 2 | 3 | 24 | F1 02 | F1 12 |
| 2 | 4 | 25 | F1 02 | F1 12 |
| 2 | 5 | 26 | F1 02 | F1 12 |
| 2 | 6 | 27 | F1 02 | F1 12 |
| 2 | 7 | 28 | F1 02 | F1 12 |
| 3 | 0 | 29 | F1 03 | F1 13 |
| 3 | 1 | 30 | F1 03 | F1 13 |
| 3 | 2 | 31 | F1 03 | F1 13 |
| 3 | 3 | 32 | F1 03 | F1 13 |
| 3 | 4 | 33 | F1 03 | F1 13 |
| 3 | 5 | 34 | F1 03 | F1 13 |
| 3 | 6 | 35 | F1 03 | F1 13 |
| 3 | 7 | 36 | F1 03 | F1 13 |
| 4 | 0 | 37 | F1 04 | F1 14 |
| 4 | 1 | 38 | F1 04 | F1 14 |
| 4 | 2 | 45 | F1 04 | F1 14 |
| 4 | 3 | 46 | F1 04 | F1 14 |
| 4 | 4 | 41 | F1 04 | F1 14 |
| 4 | 5 | 42 | F1 04 | F1 14 |
| 4 | 6 | 47 | F1 04 | F1 14 |
| 4 | 7 | 48 | F1 04 | F1 14 |
| 5 | 0 | 39 | F1 05 | F1 15 |
| 5 | 1 | 40 | F1 05 | F1 15 |
| 5 | 2 | 43 | F1 05 | F1 15 |
| 5 | 3 | 44 | F1 05 | F1 15 |
| 5 | 4 | 49 | F1 05 | F1 15 |
| 5 | 5 | 50 | F1 05 | F1 15 |
| 5 | 6 | 51 | F1 05 | F1 15 |
| 5 | 7 | 52 | F1 05 | F1 15 |
| 6 | 0 | 53 | F1 06 | F1 16 |
| 6 | 1 | 54 | F1 06 | F1 16 |
| 6 | 2 | 61 | F1 06 | F1 16 |
| 6 | 3 | 62 | F1 06 | F1 16 |
| 6 | 4 | 63 | F1 06 | F1 16 |
| 6 | 5 | 55 | F1 06 | F1 16 |
| 6 | 6 | 56 | F1 06 | F1 16 |
| 6 | 7 | 57 | F1 06 | F1 16 |
| 7 | 0 | 58 | F1 07 | F1 17 |
| 7 | 1 | 59 | F1 07 | F1 17 |
| 7 | 2 | 60 | F1 07 | F1 17 |
| 7 | 3 | 64 | F1 07 | F1 17 |
| 7 | 4 | 65 | F1 07 | F1 17 |
| 7 | 5 | 66 | F1 07 | F1 17 |
| 7 | 6 | 67 | F1 07 | F1 17 |
| 7 | 7 | 68 | F1 07 | F1 17 |
| 8 | 0 | 69 | F1 08 | F1 18 |
| 8 | 1 | 70 | F1 08 | F1 18 |
| 8 | 2 | 71 | F1 08 | F1 18 |
| 8 | 3 | 72 | F1 08 | F1 18 |
| 8 | 4 | 73 | F1 08 | F1 18 |
| 8 | 5 | 122 | F1 08 | F1 18 |
| 8 | 6 | 121 | F1 08 | F1 18 |
| 8 | 7 | 76 | F1 08 | F1 18 |
| 9 | 1 | 120 | F1 09 | F1 19 |
| 9 | 2 | 79 | F1 09 | F1 19 |
| 9 | 3 | 80 | F1 09 | F1 19 |
| 9 | 4 | 81 | F1 09 | F1 19 |
| 9 | 5 | 82 | F1 09 | F1 19 |
| 9 | 6 | 83 | F1 09 | F1 19 |
| 9 | 7 | 84 | F1 09 | F1 19 |
| 10 | 0 | 85 | F1 0A | F1 1A |
| 10 | 3 | 86 | F1 0A | F1 1A |
| 10 | 4 | 87 | F1 0A | F1 1A |
| 10 | 5 | 88 | F1 0A | F1 1A |
| 11 | 0 | 89 | F1 0B | F1 1B |
| 11 | 1 | 90 | F1 0B | F1 1B |
| 11 | 2 | 91 | F1 0B | F1 1B |
| 11 | 3 | 92 | F1 0B | F1 1B |
| 11 | 4 | 93 | F1 0B | F1 1B |
| 11 | 5 | 94 | F1 0B | F1 1B |
| 11 | 6 | 95 | F1 0B | F1 1B |
| 11 | 7 | 96 | F1 0B | F1 1B |
| 12 | 0 | 97 | F1 0C | F1 1C |
| 12 | 1 | 98 | F1 0C | F1 1C |
| 12 | 2 | 116 | F1 0C | F1 1C |
| 12 | 4 | 99 | F1 0C | F1 1C |
| 12 | 5 | 100 | F1 0C | F1 1C |
| 12 | 7 | 117 | F1 0C | F1 1C |
| 13 | 0 | 101 | F1 0D | F1 1D |
| 13 | 1 | 102 | F1 0D | F1 1D |
| 13 | 2 | 103 | F1 0D | F1 1D |
| 13 | 3 | 104 | F1 0D | F1 1D |
| 13 | 4 | 125 | F1 0D | F1 1D |
| 13 | 5 | 105 | F1 0D | F1 1D |
| 13 | 6 | 106 | F1 0D | F1 1D |
| 13 | 7 | 107 | F1 0D | F1 1D |
| 14 | 0 | 114 | F1 0E | F1 1E |
| 14 | 1 | 108 | F1 0E | F1 1E |
| 14 | 2 | 109 | F1 0E | F1 1E |
| 14 | 3 | 115 | F1 0E | F1 1E |
| 14 | 4 | 126 | F1 0E | F1 1E |
| 14 | 5 | 110 | F1 0E | F1 1E |
| 14 | 6 | 111 | F1 0E | F1 1E |
| 14 | 7 | 112 | F1 0E | F1 1E |
| 15 | 0 | 5 | F1 0F | F1 1F |
| 15 | 1 | 8 | F1 0F | F1 1F |
| 15 | 2 | 13 | F1 0F | F1 1F |
| 15 | 3 | 16 | F1 0F | F1 1F |
| 15 | 4 | 19 | F1 0F | F1 1F |
| 15 | 5 | 78 | F1 0F | F1 1F |

### 13.7 CB9E — 4-cyl, V, diag >= 'P'

| Байт | Бит | DTC № | Запрос для блока 0 | Запрос для блока 1 |
| --- | --- | --- | --- | --- |
| 0 | 0 | 2 | F1 00 | F1 10 |
| 0 | 1 | 3 | F1 00 | F1 10 |
| 0 | 2 | 4 | F1 00 | F1 10 |
| 0 | 3 | 6 | F1 00 | F1 10 |
| 0 | 4 | 7 | F1 00 | F1 10 |
| 0 | 5 | 9 | F1 00 | F1 10 |
| 0 | 6 | 10 | F1 00 | F1 10 |
| 1 | 2 | 113 | F1 01 | F1 11 |
| 1 | 3 | 14 | F1 01 | F1 11 |
| 1 | 4 | 15 | F1 01 | F1 11 |
| 1 | 5 | 17 | F1 01 | F1 11 |
| 1 | 6 | 18 | F1 01 | F1 11 |
| 1 | 7 | 20 | F1 01 | F1 11 |
| 2 | 0 | 21 | F1 02 | F1 12 |
| 2 | 1 | 22 | F1 02 | F1 12 |
| 2 | 2 | 23 | F1 02 | F1 12 |
| 2 | 3 | 24 | F1 02 | F1 12 |
| 2 | 4 | 25 | F1 02 | F1 12 |
| 3 | 0 | 29 | F1 03 | F1 13 |
| 3 | 1 | 30 | F1 03 | F1 13 |
| 3 | 6 | 35 | F1 03 | F1 13 |
| 3 | 7 | 36 | F1 03 | F1 13 |
| 4 | 0 | 37 | F1 04 | F1 14 |
| 4 | 1 | 38 | F1 04 | F1 14 |
| 4 | 2 | 39 | F1 04 | F1 14 |
| 4 | 3 | 40 | F1 04 | F1 14 |
| 4 | 4 | 41 | F1 04 | F1 14 |
| 4 | 5 | 42 | F1 04 | F1 14 |
| 4 | 6 | 43 | F1 04 | F1 14 |
| 4 | 7 | 44 | F1 04 | F1 14 |
| 5 | 4 | 49 | F1 05 | F1 15 |
| 5 | 5 | 50 | F1 05 | F1 15 |
| 5 | 6 | 51 | F1 05 | F1 15 |
| 5 | 7 | 52 | F1 05 | F1 15 |
| 6 | 0 | 53 | F1 06 | F1 16 |
| 6 | 1 | 54 | F1 06 | F1 16 |
| 6 | 2 | 55 | F1 06 | F1 16 |
| 6 | 3 | 56 | F1 06 | F1 16 |
| 6 | 4 | 57 | F1 06 | F1 16 |
| 6 | 5 | 58 | F1 06 | F1 16 |
| 6 | 6 | 59 | F1 06 | F1 16 |
| 6 | 7 | 60 | F1 06 | F1 16 |
| 7 | 3 | 64 | F1 07 | F1 17 |
| 7 | 5 | 66 | F1 07 | F1 17 |
| 7 | 6 | 67 | F1 07 | F1 17 |
| 8 | 1 | 70 | F1 08 | F1 18 |
| 8 | 2 | 71 | F1 08 | F1 18 |
| 8 | 3 | 145 | F1 08 | F1 18 |
| 8 | 4 | 146 | F1 08 | F1 18 |
| 8 | 5 | 147 | F1 08 | F1 18 |
| 8 | 7 | 76 | F1 08 | F1 18 |
| 9 | 2 | 79 | F1 09 | F1 19 |
| 9 | 4 | 81 | F1 09 | F1 19 |
| 9 | 5 | 82 | F1 09 | F1 19 |
| 9 | 7 | 84 | F1 09 | F1 19 |
| 10 | 3 | 86 | F1 0A | F1 1A |
| 10 | 4 | 87 | F1 0A | F1 1A |
| 11 | 0 | 89 | F1 0B | F1 1B |
| 11 | 1 | 90 | F1 0B | F1 1B |
| 11 | 4 | 148 | F1 0B | F1 1B |
| 11 | 5 | 149 | F1 0B | F1 1B |
| 12 | 0 | 97 | F1 0C | F1 1C |
| 12 | 1 | 98 | F1 0C | F1 1C |
| 12 | 2 | 116 | F1 0C | F1 1C |
| 12 | 3 | 150 | F1 0C | F1 1C |
| 12 | 4 | 151 | F1 0C | F1 1C |
| 12 | 5 | 159 | F1 0C | F1 1C |
| 12 | 6 | 152 | F1 0C | F1 1C |
| 12 | 7 | 117 | F1 0C | F1 1C |
| 13 | 1 | 102 | F1 0D | F1 1D |
| 13 | 2 | 103 | F1 0D | F1 1D |
| 13 | 3 | 104 | F1 0D | F1 1D |
| 13 | 4 | 125 | F1 0D | F1 1D |
| 13 | 7 | 107 | F1 0D | F1 1D |
| 14 | 0 | 114 | F1 0E | F1 1E |
| 14 | 1 | 153 | F1 0E | F1 1E |
| 14 | 2 | 154 | F1 0E | F1 1E |
| 14 | 3 | 115 | F1 0E | F1 1E |
| 14 | 4 | 126 | F1 0E | F1 1E |
| 14 | 5 | 110 | F1 0E | F1 1E |
| 14 | 6 | 111 | F1 0E | F1 1E |
| 14 | 7 | 112 | F1 0E | F1 1E |
| 15 | 0 | 5 | F1 0F | F1 1F |
| 15 | 1 | 8 | F1 0F | F1 1F |
| 15 | 2 | 13 | F1 0F | F1 1F |
| 15 | 3 | 16 | F1 0F | F1 1F |
| 15 | 4 | 19 | F1 0F | F1 1F |
| 15 | 6 | 155 | F1 0F | F1 1F |
| 15 | 7 | 127 | F1 0F | F1 1F |
| 16 | 0 | 128 | F1 30 | F1 40 |
| 16 | 1 | 129 | F1 30 | F1 40 |
| 16 | 2 | 130 | F1 30 | F1 40 |
| 16 | 3 | 131 | F1 30 | F1 40 |
| 16 | 4 | 132 | F1 30 | F1 40 |
| 16 | 5 | 156 | F1 30 | F1 40 |
| 16 | 6 | 157 | F1 30 | F1 40 |
| 16 | 7 | 160 | F1 30 | F1 40 |
| 17 | 0 | 133 | F1 31 | F1 41 |
| 17 | 1 | 134 | F1 31 | F1 41 |
| 17 | 2 | 135 | F1 31 | F1 41 |
| 17 | 3 | 161 | F1 31 | F1 41 |
| 17 | 4 | 136 | F1 31 | F1 41 |
| 17 | 6 | 137 | F1 31 | F1 41 |
| 17 | 7 | 138 | F1 31 | F1 41 |
| 18 | 0 | 139 | F1 32 | F1 42 |
| 18 | 1 | 140 | F1 32 | F1 42 |
| 18 | 2 | 141 | F1 32 | F1 42 |
| 18 | 3 | 162 | F1 32 | F1 42 |
| 18 | 4 | 158 | F1 32 | F1 42 |
| 18 | 5 | 144 | F1 32 | F1 42 |
| 18 | 6 | 163 | F1 32 | F1 42 |
| 18 | 7 | 164 | F1 32 | F1 42 |

## 14. Полная текстовая карта DTC HHT-WIN (242 заводские записи)

В базе HHT-WIN найдено **242 записи текстовых правил**, но многие номера повторяются с дополнительными условиями по диапазонам A/B, компоненту и варианту. Это 242 правила, а не 242 уникальные неисправности.
| DS | Код мин. | Код макс. | Диапазон A | Диапазон B | Флаги | EN | DE | Компонент | Помощь |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0xa08a | 2 | 2 | [0,255] | [0,255] | 0x0000 | Coolant temperature sensor ⌐¬ | Temperaturfühler Kühlmittel ⌐¬ | B11/3 | [6,585,6,585] |
| 0xa0aa | 3 | 3 | [0,255] | [0,255] | 0x0000 | Coolant temperature sensor -//- | Temperaturfühler Kühlmittel -//- | B11/3 | [6,585,6,585] |
| 0xa0ca | 4 | 4 | [0,255] | [0,255] | 0x0000 | Coolant temperature sensor implausible | Temperaturfühler Kühlmittel unplausibel | B11/3 | [6,585,6,585] |
| 0xa0ea | 5 | 5 | [0,255] | [0,255] | 0x0000 | Coolant temperature sensor Loose contact | Temperaturfühler Kühlmittel Wackelkontakt | B11/3 | [547,585,547,585] |
| 0xa10a | 6 | 6 | [0,255] | [0,255] | 0x0000 | Intake air temperature sensor ⌐¬ | Temperaturfühler Ansaugluft ⌐¬ | B17 | [6,585,6,585] |
| 0xa12a | 7 | 7 | [0,255] | [0,255] | 0x0000 | Intake air temperature sensor -//- | Temperaturfühler Ansaugluft -//- | B17 | [6,585,6,585] |
| 0xa14a | 8 | 8 | [0,255] | [0,255] | 0x0000 | Intake air temperature sensor Loose contact | Temperaturfühler Ansaugluft Wackelkontakt | B17 | [547,585,547,585] |
| 0xa16a | 9 | 9 | [0,5] | [0,255] | 0x0000 | Hot-film air mass sensor implausibly high | Heißfilm-Luftmassen- messer unplausibel hoch | B2/5 | [6,585,6,585] |
| 0xa18a | 9 | 9 | [6,9] | [0,255] | 0x0000 | Hot-film air mass sensor implausibly high | Heißfilm-Luftmassen- messer unplausibel hoch | B2/5 | [6,585,6,585] |
| 0xa1aa | 10 | 10 | [0,5] | [0,255] | 0x0000 | Hot-film air mass sensor -//- | Heißfilm-Luftmassen- messer -//- | B2/5 | [6,585,6,585] |
| 0xa1ca | 10 | 10 | [6,9] | [0,255] | 0x0000 | Hot-film air mass sensor -//- | Heißfilm-Luftmassen- messer -//- | B2/5 | [6,585,6,585] |
| 0xa1ea | 11 | 11 | [0,255] | [0,255] | 0x0000 | Idle speed contact Throttle valve pos. implausibly large | Leerlaufkontakt Drosselklappenstellung unplausibel groß | — | [6,585,6,585] |
| 0xa20a | 12 | 12 | [0,255] | [0,255] | 0x0000 | Idle speed contact Air mass implausibly large | Leerlaufkontakt Luftmasse unplausibel groß | — | [6,585,6,585] |
| 0xa22a | 13 | 13 | [0,255] | [0,255] | 0x0000 | Idle speed contact Loose contact | Leerlaufkontakt Wackelkontakt | — | [547,585,547,585] |
| 0xa24a | 14 | 14 | [0,255] | [0,255] | 0x0000 | Act val potentiometer throttle valve implausibly high | Istwertpotentiometer Drosselklappe unplausibel hoch | — | [6,585,6,585] |
| 0xa26a | 15 | 15 | [0,255] | [0,255] | 0x0000 | Act val potentiometer throttle valve implausibly low | Istwertpotentiometer Drosselklappe unplausibel niedrig | — | [6,585,6,585] |
| 0xa28a | 16 | 16 | [0,255] | [0,255] | 0x0000 | ,Act val potentiometer throttle valve Loose contact | Istwertpotentiometer Drosselklappe Wackelkontakt | — | [547,585,547,585] |
| 0xa2aa | 17 | 17 | [0,255] | [0,255] | 0x0000 | Act val potentiometer throttle valve drive implausibly high | Istwertpotentiometer Antrieb Drosselklappe unplausibel hoch | — | [0,585,0,585] |
| 0xa2ca | 18 | 18 | [0,255] | [0,255] | 0x0000 | Act val potentiometer throttle valve drive implausibly low | Istwertpotentiometer Antrieb Drosselklappe unplausibel niedrig | — | [0,585,0,585] |
| 0xa2ea | 19 | 19 | [0,255] | [0,255] | 0x0000 | Act val potentiometer throttle valve drive Loose contact | Istwertpotentiometer Antrieb Drosselklappe Wackelkontakt | — | [547,585,547,585] |
| 0xa30a | 20 | 20 | [0,255] | [0,255] | 0x0000 | Idle speed control at lower control stop | Leerlaufregelung am unteren Regelanschlag | — | [548,0,548,0] |
| 0xa32a | 21 | 21 | [0,255] | [0,255] | 0x0000 | Idle speed control at upper control stop | Leerlaufregelung am oberen Regelanschlag | — | [548,0,548,0] |
| 0xa34a | 22 | 22 | [0,255] | [0,255] | 0x0000 | CC,EFP, actuator signals emergency mode | TPM,EFP,Stellglied meldet Notlaufbetrieb | — | [548,0,548,0] |
| 0xa36a | 23 | 23 | [0,255] | [0,255] | 0x0000 | O2 sensor upstream of CAT Sensor volt. too large | O2-Sonde vor KAT Sondenspannung zu groß | G3/2 | [6,585,6,585] |
| 0xa38a | 24 | 24 | [0,255] | [0,255] | 0x0000 | O2 sensor upstream of CAT Sensor cold, -//- | O2-Sonde vor KAT Sonde kalt,-//- | G3/2 | [6,585,6,585] |
| 0xa3aa | 25 | 25 | [0,255] | [0,255] | 0x0000 | O2 sensor upstream of CAT Sensor signal implaus. | O2-Sonde vor KAT Sondensignal unplausibel | G3/2 | [6,585,6,585] |
| 0xa3ca | 26 | 26 | [0,255] | [0,255] | 0x0000 | O2 sensor down- stream of CAT Sensor volt. too large | O2-Sonde nach KAT Sondenspannung zu groß | G3/1 | [6,585,6,585] |
| 0xa3ea | 27 | 27 | [0,255] | [0,255] | 0x0000 | O2 sensor down- stream of CAT Sensor cold,-//- | O2-Sonde nach KAT Sonde kalt,-//- | G3/1 | [6,585,6,585] |
| 0xa40a | 28 | 28 | [0,255] | [0,255] | 0x0000 | O2 sensor down- stream of CAT Sensor signal implaus. | O2-Sonde nach KAT Sondensignal unplausibel | G3/1 | [6,585,6,585] |
| 0xa42a | 29 | 29 | [0,5] | [0,255] | 0x0000 | O2 sensor heater up- stream of CAT Heater curr. too small | O2-Sondenheizung vor KAT Heizstrom zu klein | G3/2 | [0,585,0,585] |
| 0xa44a | 29 | 29 | [6,9] | [0,255] | 0x0000 | O2 sensor heater up- stream of CAT Heater curr. too small | O2-Sondenheizung vor KAT Heizstrom zu klein | G3/2 | [6,585,6,585] |
| 0xa46a | 30 | 30 | [0,5] | [0,255] | 0x0000 | O2 sensor heater up- stream of CAT Heater curr. too large | O2-Sondenheizung vor KAT Heizstrom zu groß | G3/2 | [0,585,0,585] |
| 0xa48a | 30 | 30 | [6,9] | [0,255] | 0x0000 | O2 sensor heater up- stream of CAT Heater curr. too large | O2-Sondenheizung vor KAT Heizstrom zu groß | G3/2 | [6,585,6,585] |
| 0xa4aa | 31 | 31 | [0,5] | [0,255] | 0x0000 | O2 sensor heater up- stream of CAT Heater current ⌐¬ | O2-Sondenheizung vor KAT Heizstrom ⌐¬ | G3/2 | [0,585,0,585] |
| 0xa4ca | 31 | 31 | [6,9] | [0,255] | 0x0000 | O2 sensor heater up- stream of CAT Heater current ⌐¬ | O2-Sondenheizung vor KAT Heizstrom ⌐¬ | G3/2 | [6,585,6,585] |
| 0xa4ea | 32 | 32 | [0,5] | [0,255] | 0x0000 | O2 sensor heater down- stream of CAT Heat.current too small | O2-Sondenheizung nach KAT Heizstrom zu klein | G3/1 | [0,585,0,585] |
| 0xa50a | 32 | 32 | [6,9] | [0,255] | 0x0000 | O2 sensor heater down- stream of CAT Heat.current too small | O2-Sondenheizung nach KAT Heizstrom zu klein | G3/1 | [6,585,6,585] |
| 0xa52a | 33 | 33 | [0,5] | [0,255] | 0x0000 | O2 sensor heater down- stream of CAT Heat.current too large | O2-Sondenheizung nach KAT Heizstrom zu groß | G3/1 | [0,585,0,585] |
| 0xa54a | 33 | 33 | [6,9] | [0,255] | 0x0000 | O2 sensor heater down- stream of CAT Heat.current too large | O2-Sondenheizung nach KAT Heizstrom zu groß | G3/1 | [6,585,6,585] |
| 0xa56a | 34 | 34 | [0,5] | [0,255] | 0x0000 | O2 sensor heater down- stream of CAT Heating current ⌐¬ | O2-Sondenheizung nach KAT Heizstrom ⌐¬ | G3/1 | [0,585,0,585] |
| 0xa58a | 34 | 34 | [6,9] | [0,255] | 0x0000 | O2 sensor heater down- stream of CAT Heating current ⌐¬ | O2-Sondenheizung nach KAT Heizstrom ⌐¬ | G3/1 | [6,585,6,585] |
| 0xa5aa | 35 | 35 | [0,255] | [0,255] | 0x0000 | Lambda control Rich stop/mixture too lean | Lambdaregelung Fettanschlag/Gemisch zu mager | — | [549,0,549,0] |
| 0xa5ca | 36 | 36 | [0,255] | [0,255] | 0x0000 | Lambda control Lean stop/mixture too rich | Lambdaregelung Mageranschlag/Gemisch zu fett | — | [549,0,549,0] |
| 0xa5ea | 37 | 37 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.1 ⌐¬+ | Einspritzventil Zyl.1 ⌐¬+ | Y62y1 | [6,585,6,585] |
| 0xa60a | 38 | 38 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.1 -//-,⌐¬- | Einspritzventil Zyl.1 -//-,⌐¬- | Y62y1 | [6,585,6,585] |
| 0xa62a | 39 | 39 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.2 ⌐¬+ | Einspritzventil Zyl.2 ⌐¬+ | Y62y2 | [6,585,6,585] |
| 0xa64a | 40 | 40 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.2 -//-,⌐¬- | Einspritzventil Zyl.2 -//-,⌐¬- | Y62y2 | [6,585,6,585] |
| 0xa66a | 41 | 41 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.3 ⌐¬+ | Einspritzventil Zyl.3 ⌐¬+ | Y62y3 | [6,585,6,585] |
| 0xa68a | 42 | 42 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.3 -//-,⌐¬- | Einspritzventil Zyl.3 -//-,⌐¬- | Y62y3 | [6,585,6,585] |
| 0xa6aa | 43 | 43 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.4 ⌐¬+ | Einspritzventil Zyl.4 ⌐¬+ | Y62y4 | [6,585,6,585] |
| 0xa6ca | 44 | 44 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.4 -//-,⌐¬- | Einspritzventil Zyl.4 -//-,⌐¬- | Y62y4 | [6,585,6,585] |
| 0xa6ea | 45 | 45 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.5 ⌐¬+ | Einspritzventil Zyl.5 ⌐¬+ | Y62y5 | [6,585,6,585] |
| 0xa70a | 46 | 46 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.5 -//-,⌐¬- | Einspritzventil Zyl.5 -//-,⌐¬- | Y62y5 | [6,585,6,585] |
| 0xa72a | 47 | 47 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.6 ⌐¬+ | Einspritzventil Zyl.6 ⌐¬+ | Y62y6 | [6,585,6,585] |
| 0xa74a | 48 | 48 | [0,255] | [0,255] | 0x0000 | Injection valve cyl.6 -//-,⌐¬- | Einspritzventil Zyl.6 -//-,⌐¬- | Y62y6 | [6,585,6,585] |
| 0xa76a | 49 | 49 | [0,0] | [0,255] | 0x0000 | Self-adjustment Idle too rich | Selbstanpassung Leerlauf zu fett | — | [550,0,550,0] |
| 0xa78a | 49 | 49 | [3,3] | [0,255] | 0x0000 | Self-adjustment Idle too rich | Selbstanpassung Leerlauf zu fett | — | [550,0,550,0] |
| 0xa7aa | 49 | 49 | [6,9] | [0,255] | 0x0000 | Self-adjustment Idle too rich | Selbstanpassung Leerlauf zu fett | — | [550,0,550,0] |
| 0xa7ca | 49 | 49 | [0,255] | [0,255] | 0x0000 | Self-adjustment Idle too rich | Selbstanpassung Leerlauf zu fett | — | [550,0,550,0] |
| 0xa7ea | 50 | 50 | [0,0] | [0,255] | 0x0000 | Self-adjustment Idle too lean | Selbstanpassung Leerlauf zu mager | — | [550,0,550,0] |
| 0xa80a | 50 | 50 | [3,3] | [0,255] | 0x0000 | Self-adjustment Idle too lean | Selbstanpassung Leerlauf zu mager | — | [550,0,550,0] |
| 0xa82a | 50 | 50 | [6,9] | [0,255] | 0x0000 | Self-adjustment Idle too lean | Selbstanpassung Leerlauf zu mager | — | [550,0,550,0] |
| 0xa84a | 50 | 50 | [0,255] | [0,255] | 0x0000 | Self-adjustment Idle too lean | Selbstanpassung Leerlauf zu mager | — | [550,0,550,0] |
| 0xa86a | 51 | 51 | [0,5] | [0,255] | 0x0000 | Self-adjustment Lower part load too rich | Selbstanpassung Untere Teillast zu fett | — | [550,0,550,0] |
| 0xa88a | 51 | 51 | [6,9] | [0,255] | 0x0000 | Self-adjustment Lower part load too rich | Selbstanpassung Untere Teillast zu fett | — | [550,0,550,0] |
| 0xa8aa | 52 | 52 | [0,5] | [0,255] | 0x0000 | Self-adjustment Lower part load too lean | Selbstanpassung Untere Teillast zu mager | — | [550,0,550,0] |
| 0xa8ca | 52 | 52 | [6,9] | [0,255] | 0x0000 | Self-adjustment Lower part load too lean | Selbstanpassung Untere Teillast zu mager | — | [550,0,550,0] |
| 0xa8ea | 53 | 53 | [0,5] | [0,255] | 0x0000 | Self-adjustment Upper part load too rich | Selbstanpassung Obere Teillast zu fett | — | [550,0,550,0] |
| 0xa90a | 53 | 53 | [6,9] | [0,255] | 0x0000 | Self-adjustment Upper part load too rich | Selbstanpassung Obere Teillast zu fett | — | [550,0,550,0] |
| 0xa92a | 54 | 54 | [0,5] | [0,255] | 0x0000 | Self-adjustment Upper part load too lean | Selbstanpassung Obere Teillast zu mager | — | [550,0,550,0] |
| 0xa94a | 54 | 54 | [6,9] | [0,255] | 0x0000 | Self-adjustment Upper part load too lean | Selbstanpassung Obere Teillast zu mager | — | [550,0,550,0] |
| 0xa96a | 55 | 55 | [0,2] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 1 misfires | Zündendstufe 1 Zyl.1 setzt aus | T1/1 | [0,585,0,585] |
| 0xa98a | 55 | 55 | [3,5] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 2 misfires | Zündendstufe 1 Zyl.2 setzt aus | T1/1 | [0,585,0,585] |
| 0xa9aa | 55 | 55 | [7,7] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 1 misfires | Zündendstufe 1 Zyl.1 setzt aus | T1/1 | [6,585,6,585] |
| 0xa9ca | 55 | 55 | [6,6] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 1 misfires | Zündendstufe 1 Zyl.1 setzt aus | T1/1 | [6,585,6,585] |
| 0xa9ea | 55 | 55 | [9,9] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 2 misfires | Zündendstufe 1 Zyl.2 setzt aus | T1/1 | [6,585,6,585] |
| 0xaa0a | 55 | 55 | [8,8] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 2 misfires | Zündendstufe 1 Zyl.2 setzt aus | T1/1 | [6,585,6,585] |
| 0xaa2a | 56 | 56 | [0,2] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 4 misfires | Zündendstufe 1 Zyl.4 setzt aus | T1/1 | [0,585,0,585] |
| 0xaa4a | 56 | 56 | [3,5] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 5 misfires | Zündendstufe 1 Zyl.5 setzt aus | T1/1 | [0,585,0,585] |
| 0xaa6a | 56 | 56 | [7,7] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 4 misfires | Zündendstufe 1 Zyl.4 setzt aus | T1/1 | [6,585,6,585] |
| 0xaa8a | 56 | 56 | [6,6] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 4 misfires | Zündendstufe 1 Zyl.4 setzt aus | T1/1 | [6,585,6,585] |
| 0xaaaa | 56 | 56 | [9,9] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 5 misfires | Zündendstufe 1 Zyl.5 setzt aus | T1/1 | [6,585,6,585] |
| 0xaaca | 56 | 56 | [8,8] | [0,255] | 0x0000 | Ign outp stage 1 Cylinder 5 misfires | Zündendstufe 1 Zyl.5 setzt aus | T1/1 | [6,585,6,585] |
| 0xaaea | 57 | 57 | [0,5] | [0,255] | 0x0000 | Ign outp stage 1 Current value not reached | Zündendstufe 1 Stromwert nicht erreicht | T1/1 | [0,585,0,585] |
| 0xab0a | 57 | 57 | [7,7] | [0,255] | 0x0000 | Ign outp stage 1 Current value not reached | Zündendstufe 1 Stromwert nicht erreicht | T1/1 | [6,585,6,585] |
| 0xab2a | 57 | 57 | [6,6] | [0,255] | 0x0000 | Ign outp stage 1 Current value not reached | Zündendstufe 1 Stromwert nicht erreicht | T1/1 | [6,585,6,585] |
| 0xab4a | 57 | 57 | [9,9] | [0,255] | 0x0000 | Ign outp stage 1 Current value not reached | Zündendstufe 1 Stromwert nicht erreicht | T1/1 | [6,585,6,585] |
| 0xab6a | 57 | 57 | [8,8] | [0,255] | 0x0000 | Ign outp stage 1 Current value not reached | Zündendstufe 1 Stromwert nicht erreicht | T1/1 | [6,585,6,585] |
| 0xab8a | 58 | 58 | [0,2] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 2 misfires | Zündendstufe 2 Zyl.2 setzt aus | T1/2 | [0,585,0,585] |
| 0xabaa | 58 | 58 | [3,5] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 3 misfires | Zündendstufe 2 Zyl.3 setzt aus | T1/2 | [0,585,0,585] |
| 0xabca | 58 | 58 | [7,7] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 2 misfires | Zündendstufe 2 Zyl.2 setzt aus | T1/2 | [6,585,6,585] |
| 0xabea | 58 | 58 | [6,6] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 2 misfires | Zündendstufe 2 Zyl.2 setzt aus | T1/2 | [6,585,6,585] |
| 0xac0a | 58 | 58 | [9,9] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 3 misfires | Zündendstufe 2 Zyl.3 setzt aus | T1/2 | [6,585,6,585] |
| 0xac2a | 58 | 58 | [8,8] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 3 misfires | Zündendstufe 2 Zyl.3 setzt aus | T1/2 | [6,585,6,585] |
| 0xac4a | 59 | 59 | [0,2] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 3 misfires | Zündendstufe 2 Zyl.3 setzt aus | T1/2 | [0,585,0,585] |
| 0xac6a | 59 | 59 | [3,5] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 4 misfires | Zündendstufe 2 Zyl.4 setzt aus | T1/2 | [0,585,0,585] |
| 0xac8a | 59 | 59 | [7,7] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 3 misfires | Zündendstufe 2 Zyl.3 setzt aus | T1/2 | [6,585,6,585] |
| 0xacaa | 59 | 59 | [6,6] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 3 misfires | Zündendstufe 2 Zyl.3 setzt aus | T1/2 | [6,585,6,585] |
| 0xacca | 59 | 59 | [9,9] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 4 misfires | Zündendstufe 2 Zyl.4 setzt aus | T1/2 | [6,585,6,585] |
| 0xacea | 59 | 59 | [8,8] | [0,255] | 0x0000 | Ign outp stage 2 Cylinder 4 misfires | Zündendstufe 2 Zyl.4 setzt aus | T1/2 | [6,585,6,585] |
| 0xad0a | 60 | 60 | [0,5] | [0,255] | 0x0000 | Ign outp stage 2 Current value not reached | Zündendstufe 2 Stromwert nicht erreicht | T1/2 | [0,585,0,585] |
| 0xad2a | 60 | 60 | [7,7] | [0,255] | 0x0000 | Ign outp stage 2 Current value not reached | Zündendstufe 2 Stromwert nicht erreicht | T1/2 | [6,585,6,585] |
| 0xad4a | 60 | 60 | [6,6] | [0,255] | 0x0000 | Ign outp stage 2 Current value not reached | Zündendstufe 2 Stromwert nicht erreicht | T1/2 | [6,585,6,585] |
| 0xad6a | 60 | 60 | [9,9] | [0,255] | 0x0000 | Ign outp stage 2 Current value not reached | Zündendstufe 2 Stromwert nicht erreicht | T1/2 | [6,585,6,585] |
| 0xad8a | 60 | 60 | [8,8] | [0,255] | 0x0000 | Ign outp stage 2 Current value not reached | Zündendstufe 2 Stromwert nicht erreicht | T1/2 | [6,585,6,585] |
| 0xadaa | 61 | 61 | [0,5] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 1 misfires | Zündendstufe 3 Zyl.1 setzt aus | T1/3 | [0,585,0,585] |
| 0xadca | 61 | 61 | [7,7] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 1 misfires | Zündendstufe 3 Zyl.1 setzt aus | T1/3 | [6,585,6,585] |
| 0xadea | 61 | 61 | [6,6] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 1 misfires | Zündendstufe 3 Zyl.1 setzt aus | T1/3 | [6,585,6,585] |
| 0xae0a | 61 | 61 | [9,9] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 1 misfires | Zündendstufe 3 Zyl.1 setzt aus | T1/3 | [6,585,6,585] |
| 0xae2a | 61 | 61 | [8,8] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 1 misfires | Zündendstufe 3 Zyl.1 setzt aus | T1/3 | [6,585,6,585] |
| 0xae4a | 62 | 62 | [0,5] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 6 misfires | Zündendstufe 3 Zyl.6 setzt aus | T1/3 | [0,585,0,585] |
| 0xae6a | 62 | 62 | [7,7] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 6 misfires | Zündendstufe 3 Zyl.6 setzt aus | T1/3 | [6,585,6,585] |
| 0xae8a | 62 | 62 | [6,6] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 6 misfires | Zündendstufe 3 Zyl.6 setzt aus | T1/3 | [6,585,6,585] |
| 0xaeaa | 62 | 62 | [9,9] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 6 misfires | Zündendstufe 3 Zyl.6 setzt aus | T1/3 | [6,585,6,585] |
| 0xaeca | 62 | 62 | [8,8] | [0,255] | 0x0000 | Ign outp stage 3 Cylinder 6 misfires | Zündendstufe 3 Zyl.6 setzt aus | T1/3 | [6,585,6,585] |
| 0xaeea | 63 | 63 | [0,5] | [0,255] | 0x0000 | Ign outp stage 3 Current value not reached | Zündendstufe 3 Stromwert nicht erreicht | T1/3 | [0,585,0,585] |
| 0xaf0a | 63 | 63 | [7,7] | [0,255] | 0x0000 | Ign outp stage 3 Current value not reached | Zündendstufe 3 Stromwert nicht erreicht | T1/3 | [6,585,6,585] |
| 0xaf2a | 63 | 63 | [6,6] | [0,255] | 0x0000 | Ign outp stage 3 Current value not reached | Zündendstufe 3 Stromwert nicht erreicht | T1/3 | [6,585,6,585] |
| 0xaf4a | 63 | 63 | [9,9] | [0,255] | 0x0000 | Ign outp stage 3 Current value not reached | Zündendstufe 3 Stromwert nicht erreicht | T1/3 | [6,585,6,585] |
| 0xaf6a | 63 | 63 | [8,8] | [0,255] | 0x0000 | Ign outp stage 3 Current value not reached | Zündendstufe 3 Stromwert nicht erreicht | T1/3 | [6,585,6,585] |
| 0xaf8a | 64 | 64 | [0,255] | [0,255] | 0x0000 | Crankshaft signal not recognized/ implausible | Signal Kurbelwelle nicht erkannt/ unplausibel | L5 | [0,585,0,585] |
| 0xafaa | 65 | 65 | [0,255] | [0,255] | 0x0000 | Crankshaft signal Magnet missing/Number of teeth implausible | Signal Kurbelwelle Magnet fehlt/Zähnezahl unplausibel | L5 | [0,585,0,585] |
| 0xafca | 66 | 66 | [0,255] | [0,255] | 0x0000 | Crankshaft signal Speed implausibly high | Signal Kurbelwelle Drehzahl unplausibel hoch | L5 | [0,585,0,585] |
| 0xafea | 67 | 67 | [0,255] | [0,255] | 0x0000 | Camshaft signal implausible/not recognized | Signal Nockenwelle unplausibel/nicht erkannt | L5/1 | [0,585,0,585] |
| 0xb00a | 68 | 68 | [0,255] | [0,255] | 0x0000 | HFM res. trimming plug ⌐¬- | Abgleichstecker HFM ⌐¬- | R16/5 | [6,585,6,585] |
| 0xb02a | 69 | 69 | [0,255] | [0,255] | 0x0000 | HFM res. trimming plug -//-,⌐¬+ | Abgleichstecker HFM -//-,⌐¬+ | R16/5 | [6,585,6,585] |
| 0xb04a | 70 | 70 | [0,255] | [0,255] | 0x0000 | TN speed signal Output ⌐¬- | Drehzahlsignal TN Ausgang ⌐¬- | — | [6,585,6,585] |
| 0xb06a | 71 | 71 | [0,255] | [0,255] | 0x0000 | TN speed signal Output ⌐¬+ | Drehzahlsignal TN Ausgang ⌐¬+ | — | [6,585,6,585] |
| 0xb08a | 72 | 72 | [10,10] | [0,255] | 0x0000 | Road speed signal not recognized | Geschwindigkeitssignal nicht erkannt | — | [6,585,6,585] |
| 0xb0aa | 72 | 72 | [0,255] | [0,255] | 0x0000 | Road speed signal not recognized | Geschwindigkeitssignal nicht erkannt | — | [6,585,6,585] |
| 0xb0ca | 73 | 73 | [10,10] | [0,255] | 0x0000 | Road speed signal implausibly high | Geschwindigkeitssignal unplausibel hoch | — | [6,585,6,585] |
| 0xb0ea | 73 | 73 | [0,255] | [0,255] | 0x0000 | Road speed signal implausibly high | Geschwindigkeitssignal unplausibel hoch | — | [6,585,6,585] |
| 0xb10a | 74 | 74 | [0,255] | [0,255] | 0x0000 | PSV relay ⌐¬+ | Relais PSV ⌐¬+ | K3/1 | [6,585,6,585] |
| 0xb12a | 75 | 75 | [0,255] | [0,255] | 0x0000 | PSV relay -//-,⌐¬- | Relais PSV -//-,⌐¬- | K3/1 | [6,585,6,585] |
| 0xb14a | 76 | 76 | [0,255] | [0,255] | 0x0000 | Fuel pump relay -//-,⌐¬ | Relais Kraftstoff- pumpen -//-,⌐¬ | K27 | [0,585,0,585] |
| 0xb16a | 77 | 77 | [0,255] | [0,255] | 0x0000 | CO potentiometer Input ⌐¬+ | CO-Potentiometer Eingang ⌐¬+ | R33 | [6,585,6,585] |
| 0xb18a | 78 | 78 | [0,255] | [0,255] | 0x0000 | CO potentiometer Loose contact | CO-Potentiometer Wackelkontakt | R33 | [6,585,6,585] |
| 0xb1aa | 79 | 79 | [0,5] | [0,255] | 0x0000 | Knock sensor 1 signal -//- | Signal Klopfsensor 1 -//- | — | [551,0,551,0] |
| 0xb1ca | 79 | 79 | [6,7] | [0,255] | 0x0000 | Knock sensor 1 signal -//- | Signal Klopfsensor 1 -//- | — | [551,0,551,0] |
| 0xb1ea | 79 | 79 | [8,9] | [0,255] | 0x0000 | Knock sensor 1 signal -//- | Signal Klopfsensor 1 -//- | — | [551,0,551,0] |
| 0xb20a | 80 | 80 | [0,5] | [0,255] | 0x0000 | Knock sensor 2 signal -//- | Signal Klopfsensor 2 -//- | — | [551,0,551,0] |
| 0xb22a | 80 | 80 | [6,7] | [0,255] | 0x0000 | Knock sensor 2 signal -//- | Signal Klopfsensor 2 -//- | — | [551,0,551,0] |
| 0xb24a | 80 | 80 | [8,9] | [0,255] | 0x0000 | Knock sensor 2 signal -//- | Signal Klopfsensor 2 -//- | — | [551,0,551,0] |
| 0xb26a | 81 | 81 | [0,5] | [0,255] | 0x0000 | Max. retardation reached at at least 1 cylinder | Maximale Spätver- stellung an mindestens 1 Zyl. erreicht | — | [552,0,552,0] |
| 0xb28a | 81 | 81 | [6,7] | [0,255] | 0x0000 | Max. retardation reached at at least 1 cylinder | Maximale Spätver- stellung an mindestens 1 Zyl. erreicht | — | [552,0,552,0] |
| 0xb2aa | 81 | 81 | [8,9] | [0,255] | 0x0000 | Max. retardation reached at at least 1 cylinder | Maximale Spätver- stellung an mindestens 1 Zyl. erreicht | — | [552,0,552,0] |
| 0xb2ca | 82 | 82 | [0,5] | [0,255] | 0x0000 | Ignition angle deviation too high | Zündwinkelabweichung zu groß | — | [552,0,552,0] |
| 0xb2ea | 82 | 82 | [6,7] | [0,255] | 0x0000 | Ignition angle deviation too high | Zündwinkelabweichung zu groß | — | [552,0,552,0] |
| 0xb30a | 82 | 82 | [8,9] | [0,255] | 0x0000 | Ignition angle deviation too high | Zündwinkelabweichung zu groß | — | [552,0,552,0] |
| 0xb32a | 83 | 83 | [0,5] | [0,255] | 0x0000 | Knock ctrl analysis, control unit defective | Klopfregelauswerte- schaltung Steuergerät defekt | N3/4 | [553,0,553,0] |
| 0xb34a | 83 | 83 | [6,7] | [0,255] | 0x0000 | Knock ctrl analysis, control unit defective | Klopfregelauswerte- schaltung Steuergerät defekt | N3/4 | [553,0,553,0] |
| 0xb36a | 83 | 83 | [8,9] | [0,255] | 0x0000 | Knock ctrl analysis, control unit defective | Klopfregelauswerte- schaltung Steuergerät defekt | N3/4 | [553,0,553,0] |
| 0xb38a | 84 | 84 | [0,255] | [0,255] | 0x0000 | Short-term self- adjustment idle speed/ part load exceeded | Kurzzeitige Selbstan- passung Leerlauf/Teil- last überschritten | — | [554,0,554,0] |
| 0xb3aa | 85 | 85 | [0,255] | [0,255] | 0x0000 | Air pump control units output -//-,⌐¬ | Luftpumpe Steuergeräte Ausgang -//-,⌐¬ | — | [6,585,6,585] |
| 0xb3ca | 86 | 86 | [0,5] | [0,255] | 0x0000 | Regeneration switch- over valve -//-,⌐¬ | Umschaltventil Regenerierung -//-,⌐¬ | Y58/1 | [0,585,0,585] |
| 0xb3ea | 86 | 86 | [6,9] | [0,255] | 0x0000 | Regeneration switch- over valve -//-,⌐¬ | Umschaltventil Regenerierung -//-,⌐¬ | Y58/1 | [6,585,6,585] |
| 0xb40a | 87 | 87 | [0,5] | [0,255] | 0x0000 | Regeneration switch- over valve ⌐¬+ | Umschaltventil Regenerierung ⌐¬+ | Y58/1 | [0,585,0,585] |
| 0xb42a | 87 | 87 | [6,9] | [0,255] | 0x0000 | Regeneration switch- over valve ⌐¬+ | Umschaltventil Regenerierung ⌐¬+ | Y58/1 | [6,585,6,585] |
| 0xb44a | 88 | 88 | [0,5] | [0,255] | 0x0000 | Shift point retard sw.over valve -//-,⌐¬ | Umschaltventil Schalt- punktanhebung -//-,⌐¬ | Y3/3 | [0,585,0,585] |
| 0xb46a | 88 | 88 | [6,9] | [0,255] | 0x0000 | Shift point retard sw.over valve -//-,⌐¬ | Umschaltventil Schalt- punktanhebung -//-,⌐¬ | Y3/3 | [6,585,6,585] |
| 0xb48a | 89 | 89 | [0,255] | [0,255] | 0x0000 | Camshaft timing actuator ⌐¬+ | Stellmagnet Nocken- wellensteuerung ⌐¬+ | Y49 | [6,585,6,585] |
| 0xb4aa | 90 | 90 | [0,255] | [0,255] | 0x0000 | Camshaft timing actuator -//-,⌐¬- | Stellmagnet Nocken- wellensteuerung -//-,⌐¬- | Y49 | [6,585,6,585] |
| 0xb4ca | 91 | 91 | [0,255] | [0,255] | 0x0000 | EGR sw.over valve ⌐¬+ | Umschaltventil ARF ⌐¬+ | Y27 | [6,585,6,585] |
| 0xb4ea | 92 | 92 | [0,255] | [0,255] | 0x0000 | EGR sw.over valve -//-,⌐¬- | Umschaltventil ARF -//-,⌐¬- | Y27 | [6,585,6,585] |
| 0xb50a | 93 | 93 | [0,5] | [0,255] | 0x0000 | Transmission overload prot. switch ⌐¬- | Schalter Getriebe- überlastschutz ⌐¬- | S65 | [0,585,0,585] |
| 0xb52a | 93 | 93 | [6,9] | [0,255] | 0x0000 | Transmission overload prot. switch ⌐¬- | Schalter Getriebe- überlastschutz ⌐¬- | S65 | [6,585,6,585] |
| 0xb54a | 94 | 94 | [0,5] | [0,255] | 0x0000 | Transmission overload prot. switch | Schalter Getriebe- überlastschutz | S65 | [0,585,0,585] |
| 0xb56a | 94 | 94 | [6,9] | [0,255] | 0x0000 | Transmission overload prot. switch | Schalter Getriebe- überlastschutz | S65 | [6,585,6,585] |
| 0xb58a | 95 | 95 | [0,5] | [0,255] | 0x0000 | Transmission overload prot. switch | Schalter Getriebe- überlastschutz | S65 | [0,585,0,585] |
| 0xb5aa | 95 | 95 | [6,9] | [0,255] | 0x0000 | Transmission overload prot. switch | Schalter Getriebe- überlastschutz | S65 | [6,585,6,585] |
| 0xb5ca | 96 | 96 | [0,5] | [0,255] | 0x0000 | Transmission overload prot. switch implausible | Schalter Getriebe- überlastschutz unplausibel | S65 | [0,585,0,585] |
| 0xb5ea | 96 | 96 | [6,9] | [0,255] | 0x0000 | Transmission overload prot. switch implausible | Schalter Getriebe- überlastschutz unplausibel | S65 | [6,585,6,585] |
| 0xb60a | 97 | 97 | [0,255] | [0,255] | 0x0000 | CAN: Transm. from HFM faulty | CAN:Sendung von HFM gestört | N3/4 | [0,585,0,585] |
| 0xb62a | 98 | 98 | [0,255] | [0,255] | 0x0000 | CAN: No reception from ASR | CAN:kein Empfang von ASR | — | [0,585,0,585] |
| 0xb64a | 99 | 99 | [0,255] | [0,255] | 0x0000 | CAN: No reception from EFP,TPM | CAN:kein Empfang von EFP,TPM | — | [0,585,0,585] |
| 0xb66a | 100 | 100 | [0,255] | [0,255] | 0x0000 | CAN: No reception from diagnosis module | CAN:kein Empfang von Diagnosemodul | — | [0,585,0,585] |
| 0xb68a | 101 | 101 | [0,255] | [0,255] | 0x0000 | No starter signal tml 50 | Startsignal Kl 50 fehlt | — | [6,585,6,585] |
| 0xb6aa | 102 | 102 | [0,5] | [0,255] | 0x0000 | Thermocouple CAT Temperature too high | Thermoelement KAT Temperatur zu groß | B16/6 | [0,585,0,585] |
| 0xb6ca | 102 | 102 | [6,9] | [0,255] | 0x0000 | Thermocouple CAT Temperature too high | Thermoelement KAT Temperatur zu groß | B16/6 | [6,585,6,585] |
| 0xb6ea | 103 | 103 | [0,5] | [0,255] | 0x0000 | Thermocouple CAT Temperature too low | Thermoelement KAT Temperatur zu klein | B16/6 | [0,585,0,585] |
| 0xb70a | 103 | 103 | [6,9] | [0,255] | 0x0000 | Thermocouple CAT Temperature too low | Thermoelement KAT Temperatur zu klein | B16/6 | [6,585,6,585] |
| 0xb72a | 104 | 104 | [0,5] | [0,255] | 0x0000 | Fuel safety shutoff recognized | Sicherheitskraftstoff- abschaltung erkannt | — | [0,585,0,585] |
| 0xb74a | 104 | 104 | [6,9] | [0,255] | 0x0000 | Fuel safety shutoff recognized | Sicherheitskraftstoff- abschaltung erkannt | — | [6,585,6,585] |
| 0xb76a | 105 | 105 | [0,255] | [0,255] | 0x0000 | Reson. intake manif. sw.over valve ⌐¬+ | Umschaltventil Reso- nanzsaugrohr ⌐¬+ | Y22/6 | [6,585,6,585] |
| 0xb78a | 106 | 106 | [0,255] | [0,255] | 0x0000 | Reson. intake manif. sw.over valve -//-,⌐¬- | Umschaltventil Reso- nanzsaugrohr -//-,⌐¬- | Y22/6 | [6,585,6,585] |
| 0xb7aa | 107 | 107 | [0,255] | [0,255] | 0x0000 | Dwell angle control at stop of ignition output stage ⌐¬- | Schließwinkelregelung am Anschlag Zündend- stufe ⌐¬- | — | [0,585,0,585] |
| 0xb7ca | 108 | 108 | [0,5] | [0,255] | 0x0000 | Oxygen sensor heater downstream of CAT Actuation ⌐¬+ | O2-Sondenheizung nach KAT Ansteuerung ⌐¬+ | G3/1 | [0,585,0,585] |
| 0xb7ea | 108 | 108 | [6,9] | [0,255] | 0x0000 | Oxygen sensor heater downstream of CAT Actuation ⌐¬+ | O2-Sondenheizung nach KAT Ansteuerung ⌐¬+ | G3/1 | [6,585,6,585] |
| 0xb80a | 109 | 109 | [0,5] | [0,255] | 0x0000 | Oxygen sensor heater downstream of CAT Actuation -//-,⌐¬- | O2-Sondenheizung nach KAT Ansteuerung -//-,⌐¬- | G3/1 | [0,585,0,585] |
| 0xb82a | 109 | 109 | [6,9] | [0,255] | 0x0000 | Oxygen sensor heater downstream of CAT Actuation -//-,⌐¬- | O2-Sondenheizung nach KAT Ansteuerung -//-,⌐¬- | G3/1 | [6,585,6,585] |
| 0xb84a | 110 | 110 | [0,5] | [0,255] | 0x0000 | Voltage supply to HFM control unit implausible | Spannungsversorgung am Steuergerät HFM unplausibel | N3/4 | [0,585,0,585] |
| 0xb86a | 110 | 110 | [6,9] | [0,255] | 0x0000 | Voltage supply to HFM control unit implausible | Spannungsversorgung am Steuergerät HFM unplausibel | N3/4 | [6,585,6,585] |
| 0xb88a | 111 | 111 | [0,5] | [0,255] | 0x0000 | Voltage at HFM control unit Undervoltage | Spannungsversorgung am Steuergerät HFM Unterspannung | N3/4 | [0,585,0,585] |
| 0xb8aa | 111 | 111 | [6,9] | [0,255] | 0x0000 | Voltage at HFM control unit Undervoltage | Spannungsversorgung am Steuergerät HFM Unterspannung | N3/4 | [6,585,6,585] |
| 0xb8ca | 112 | 112 | [0,255] | [0,255] | 0x0000 | HFM control unit | Steuergerät HFM | N3/4 | [553,0,553,0] |
| 0xb8ea | 113 | 113 | [0,255] | [0,255] | 0x0000 | HFM control unit not coded | Steuergerät HFM nicht codiert | N3/4 | [555,0,555,0] |
| 0xb90a | 114 | 114 | [0,255] | [0,255] | 0x0000 | HFM control unit identification implausible | Identifizierung Steuergerät HFM unplausibel | N3/4 | [555,0,555,0] |
| 0xb92a | 115 | 115 | [0,255] | [0,255] | 0x0000 | HFM control unit coding bytes implausible | Codierbytes Steuergerät HFM unplausibel | N3/4 | [555,0,555,0] |
| 0xb94a | 116 | 116 | [0,255] | [0,255] | 0x0000 | CAN: no reception from IFZ | CAN:kein Empfang von IFZ | — | [556,585,556,585] |
| 0xb96a | 117 | 117 | [0,255] | [0,255] | 0x0000 | Attempt at starting performed with IFZ locked | Startversuch bei verriegelter IFZ durchgeführt | — | [557,585,557,585] |
| 0xb98a | 118 | 118 | [0,255] | [0,255] | 0x0000 | Supercharger function implausible | Kompressor Funktion unplausibel | — | [0,585,0,585] |
| 0xb9aa | 119 | 119 | [0,255] | [0,255] | 0x0000 | Supercharger solenoid coupling -//-,⌐¬- | Magnetkupplung Kompressor -//-,⌐¬- | Y2/1 | [0,585,0,585] |
| 0xb9ca | 120 | 120 | [0,255] | [0,255] | 0x0000 | ETS signal ⌐¬- ETS fault | ETS Signal ⌐¬- ETS Fehler | — | [558,585,558,585] |
| 0xb9ea | 121 | 121 | [0,255] | [0,255] | 0x0000 | ETS signal -//-,⌐¬+ | ETS Signal -//-,⌐¬+ | — | [558,585,558,585] |
| 0xba0a | 122 | 122 | [0,255] | [0,255] | 0x0000 | Front vehicle speed signal implausible | Geschwindigkeits- signal Vorderachse unplausibel | — | [6,585,6,585] |
| 0xba2a | 123 | 123 | [0,255] | [0,255] | 0x0000 | Air flap switchover/ air filter actuator -//-,⌐¬- | Stellglied Luftklap- penumschaltung/Luft- filter -//-,⌐¬- | M16/7 | [0,585,0,585] |
| 0xba4a | 124 | 124 | [0,255] | [0,255] | 0x0000 | Air flap switchover/ air filter actuator ⌐¬+ | Stellglied Luftklap- penumschaltung/Luft- filter ⌐¬+ | M16/7 | [0,585,0,585] |
| 0xba6a | 125 | 126 | [0,255] | [0,255] | 0x0000 | HFM control unit | Steuergerät HFM | N3/4 | [553,0,553,0] |
| 0xba8a | 127 | 127 | [0,255] | [0,255] | 0x0000 | LLR and TPM/LLR actuators mixed up | Stellglied LLR und TPM/LLR vertauscht | — | [559,0,559,0] |
| 0xbaaa | 128 | 129 | [0,255] | [0,255] | 0x0000 | HFM control unit | Steuergerät HFM | N3/4 | [553,0,553,0] |
| 0xbaca | 130 | 130 | [0,255] | [0,255] | 0x0000 | Drive actual value potentiometer | Istwertpotentiometer Antrieb | — | [6,585,6,585] |
| 0xbaea | 131 | 132 | [0,255] | [0,255] | 0x0000 | HFM control unit | Steuergerät HFM | N3/4 | [553,0,553,0] |
| 0xbb0a | 133 | 133 | [0,255] | [0,255] | 0x0000 | Actuator | Stellglied | — | [560,0,560,0] |
| 0xbb2a | 134 | 134 | [0,255] | [0,255] | 0x0000 | HFM control unit | Steuergerät HFM | N3/4 | [553,0,553,0] |
| 0xbb4a | 135 | 135 | [0,255] | [0,255] | 0x0000 | Actuator potentiometer voltage supply | Spannungsversorgung Potentiometer Stell- glied | — | [0,585,0,585] |
| 0xbb6a | 136 | 136 | [0,255] | [0,255] | 0x0000 | Drive actual valve potentiometer Active test | Istwertpotentiometer Antrieb Aktiv-Prüfung | — | [6,585,6,585] |
| 0xbb8a | 137 | 137 | [0,255] | [0,255] | 0x0000 | HFM control unit | Steuergerät HFM | N3/4 | [553,0,553,0] |
| 0xbbaa | 138 | 138 | [0,255] | [0,255] | 0x0000 | Actuator | Stellglied | — | [560,0,560,0] |
| 0xbbca | 139 | 139 | [0,255] | [0,255] | 0x0000 | Cruise control switch | Tastschalter Tempomat | — | [6,585,6,585] |
| 0xbbea | 140 | 142 | [0,255] | [0,255] | 0x0000 | HFM control unit | Steuergerät HFM | N3/4 | [553,0,553,0] |
| 0xbc0a | 143 | 143 | [0,255] | [0,255] | 0x0000 | Stop light switch NO contact | Bremslichtschalter Schließer | — | [6,585,6,585] |
| 0xbc2a | 144 | 144 | [0,255] | [0,255] | 0x0000 | HFM control unit | Steuergerät HFM | N3/4 | [553,0,553,0] |
| 0xbc4a | 145 | 145 | [0,255] | [0,255] | 0x0000 | CAN: rear axle vehicle speed signal implausible | CAN:Geschwindigkeits- signal Hinterachse unplausibel | — | [0,585,0,585] |
| 0xbc6a | 146 | 146 | [0,255] | [0,255] | 0x0000 | CAN: front axle vehicle speed signal implausible | CAN:Geschwindigkeits- signal Vorderachse unplausibel | — | [0,585,0,585] |
| 0xbc8a | 147 | 147 | [0,255] | [0,255] | 0x0000 | CAN: air conditioning pressure implausible | CAN:Klimaanlage Druck unplausibel | — | [0,585,0,585] |
| 0xbcaa | 148 | 148 | [0,255] | [0,255] | 0x0000 | Starter output ⌐¬+ | Starter Ausgang ⌐¬+ | — | [561,585,561,585] |
| 0xbcca | 149 | 149 | [0,255] | [0,255] | 0x0000 | Starter output ⌐¬- | Starter Ausgang ⌐¬- | — | [561,585,561,585] |
| 0xbcea | 150 | 150 | [0,255] | [0,255] | 0x0000 | CAN: no reception from ETC | CAN:kein Empfang von EGS | — | [0,585,0,585] |
| 0xbd0a | 151 | 151 | [0,255] | [0,255] | 0x0000 | CAN: no reception from Tempmatic A/C, Automatic A/C | CAN:kein Empfang von TAU,KLA | — | [0,585,0,585] |
| 0xbd2a | 152 | 152 | [0,255] | [0,255] | 0x0000 | CAN: no reception from CLUS | CAN:kein Empfang von KI | — | [0,585,0,585] |
| 0xbd4a | 153 | 153 | [0,255] | [0,255] | 0x0000 | Electric suction fan engine / A/C output ⌐¬+ | Elektrischer Saug- lüfter Motor/Klima Ausgang ⌐¬+ | — | [562,585,562,585] |
| 0xbd6a | 154 | 154 | [0,255] | [0,255] | 0x0000 | Electric suction fan engine / A/C output ⌐¬- | Elektrischer Saug- lüfter Motor/Klima Ausgang ⌐¬- | — | [562,585,562,585] |
| 0xbd8a | 155 | 155 | [0,255] | [0,255] | 0x0000 | Transmission variant coding implausible | Codierung Getriebe- variante unplausibel | — | [563,585,563,585] |
| 0xbdaa | 156 | 156 | [0,255] | [0,255] | 0x0000 | CAN: signal from ETS/ABS implausible | CAN:Signal von ETS/ABS unplausibel | — | [0,585,0,585] |
| 0xbdca | 157 | 157 | [0,255] | [0,255] | 0x0000 | CAN: signal from ETS/ABS implausible | CAN:Signal von ETS/ABS unplausibel | — | [0,585,0,585] |
| 0xbdea | 158 | 158 | [0,255] | [0,255] | 0x0000 | CAN: brake information implausible | CAN:Bremseninformation unplausibel | — | [0,585,0,585] |
| 0xbe0a | 159 | 159 | [0,255] | [0,255] | 0x0000 | CAN:no reception from EZS | CAN:kein Empfang von EZS | — | [0,585,0,585] |
| 0xbe2a | 160 | 160 | [0,255] | [0,255] | 0x0000 | CAN:ASR Fuel shut-off implausible | CAN:ASR Kraftstoffabschaltung unplausibel | — | [0,585,0,585] |
| 0xbe4a | 161 | 161 | [0,255] | [0,255] | 0x0000 | CAN:ASR Fuel feed implausible | CAN:ASR Kraftstoffzuschaltung unplausibel | — | [0,585,0,585] |
| 0xbe6a | 162 | 162 | [0,255] | [0,255] | 0x0000 | CAN:cruise control signal from EIS implausible | CAN:Tempomatsignal von EZS unplausibel | — | [0,585,0,585] |
| 0xbe8a | 163 | 163 | [0,255] | [0,255] | 0x0000 | CAN:no cruise control switch signal from EZS | CAN:Signal Tastschal- ter Tempomat von EZS fehlt | — | [0,585,0,585] |
| 0xbeaa | 164 | 164 | [0,255] | [0,255] | 0x0000 | CAN:cruise control switch signal from EZS implausible | CAN:Signal Tastschal- ter Tempomat von EZS unplausibel | — | [0,585,0,585] |

## 15. Вариантозависимые исправления DTC и сравнение HFMScan

Правила применяются **после** извлечения базового DTC и **только** при подтверждённом диагностическом индексе. Порядок применения таблиц важен:
- `_GROUP_O_REMAP`
- `_DIAG49_BOSCH_H_REMAP`
- `_DIAG49_PLUS_REMAP`
- `_DIAG50_55_REMAP`

### _GROUP_O_REMAP

Условие: `engine_group == 'o'`.
| Базовый DTC | Скорректированный DTC |
| --- | --- |
| 51 | 55 |
| 52 | 56 |
| 53 | 57 |
| 54 | 58 |
| 55 | 59 |
| 56 | 60 |

### _DIAG49_BOSCH_H_REMAP

Условие: `diagnosis_index == 49 and manufacturer == 1 and engine_group == 'h'`.
| Базовый DTC | Скорректированный DTC |
| --- | --- |
| 35 | 45 |
| 36 | 46 |
| 37 | 41 |
| 38 | 42 |
| 39 | 47 |
| 40 | 48 |
| 41 | 39 |
| 42 | 40 |

### _DIAG49_PLUS_REMAP

Условие: `diagnosis_index >= 49`.
| Базовый DTC | Скорректированный DTC |
| --- | --- |
| 70 | 122 |
| 71 | 121 |
| 74 | 120 |

### _DIAG50_55_REMAP

Условие: `50 <= diagnosis_index <= 55`.
| Базовый DTC | Скорректированный DTC |
| --- | --- |
| 68 | 145 |
| 69 | 146 |
| 70 | 147 |
| 93 | 148 |
| 94 | 149 |
| 100 | 150 |
| 101 | 151 |
| 102 | 159 |
| 103 | 152 |
| 114 | 153 |
| 115 | 154 |
| 127 | 155 |
| 134 | 156 |
| 135 | 157 |
| 136 | 160 |
| 140 | 161 |
| 148 | 162 |
| 149 | 158 |
| 151 | 163 |
| 152 | 164 |

HFMScan содержит дополнительный каталог ошибочных и штатных состояний; некоторые строки относятся к PMS и LH. Для объединённого проекта при несовпадении русских формулировок приоритет имеет заводская HHT-строка с идентифицированной модификацией, но при неизвестном варианте сохраняются обе атрибутированные трактовки.
### 15.1 HFMScan — полный экспорт битовых/текстовых записей (включая смежные ЭБУ)

Ниже **все строки извлечённого отчёта** для прослеживаемости, с явной колонкой `system`. Реализация M104/M111 должна фильтровать только HFM.
| Система | Вариант | Бит № | Адрес 0 | Адрес 1 | Маска | Команда | DTC | Заголовок RU | Доказательство |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HFM | base | 1 | 0x00 | 0x10 | 0x01 | F1 | 2 | HFM-SFI, PEC Engine Coolant Temperature Sensor - Short Circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 2 | 0x00 | 0x10 | 0x02 | F1 | 3 | HFM-SFI, PEC Engine Coolant Temperature Sensor - Open Circuit | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 3 | 0x00 | 0x10 | 0x04 | F1 | 4 | HFM-SFI, PEC Engine Coolant Temperature Sensor - Signal Incorrect. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 4 | 0x00 | 0x10 | 0x08 | F1 | 6 | Intake Air Temperature Sensor, Short Circuit | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 5 | 0x00 | 0x10 | 0x10 | F1 | 7 | Intake Air Temperature Sensor, Open Circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 6 | 0x00 | 0x10 | 0x20 | F1 | 9 | Hot Film Air Mass Sensor - Signal Too High. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 7 | 0x00 | 0x10 | 0x40 | F1 | 10 | Hot Film Air Mass Sensor - Open Circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 8 | 0x00 | 0x10 | 0x80 | F1 | 11 | Engine idle speed contact Throttle valve position too large. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 9 | 0x01 | 0x11 | 0x01 | F1 | 118 | Функционирование механического нагнетателя : не правдоподобное значение. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 10 | 0x01 | 0x11 | 0x02 | F1 | 12 | Engine idle speed contact air mass too large. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 11 | 0x01 | 0x11 | 0x04 | F1 | 113 | HFM-SFI control unit not coded | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 12 | 0x01 | 0x11 | 0x08 | F1 | 14 | Throttle valve potentiometer actual value too high. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 13 | 0x01 | 0x11 | 0x10 | F1 | 15 | Throttle valve potentiometer actual value too low. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 14 | 0x01 | 0x11 | 0x20 | F1 | 17 | Throttle valve potentiometer drive value implausibly high. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 15 | 0x01 | 0x11 | 0x40 | F1 | 18 | Throttle valve potentiometer drive value implausibly low. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 16 | 0x01 | 0x11 | 0x80 | F1 | 20 | ISC (Idle speed control at lower control stop area, malfunction. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 17 | 0x02 | 0x12 | 0x01 | F1 | 21 | ISC (Idle speed control at upper control stop area, malfunction. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 18 | 0x02 | 0x12 | 0x02 | F1 | 22 | CC,EFP actuator signals in limp home mode (emergency mode). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 19 | 0x02 | 0x12 | 0x04 | F1 | 23 | O2 sensor (before/upstream of Cat. Conv.), voltage too large. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 20 | 0x02 | 0x12 | 0x08 | F1 | 24 | O2 sensor (before/upstream of Cat. Conv.), open circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 21 | 0x02 | 0x12 | 0x10 | F1 | 25 | O2 sensor (before/upstream of Cat. Conv.), signal incorrect. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 22 | 0x02 | 0x12 | 0x20 | F1 | 26 | O2 sensor (after/downstream of Cat. Conv.), voltage too large. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 23 | 0x02 | 0x12 | 0x40 | F1 | 27 | O2 sensor (after/downstream of Cat. Conv.), open circuit | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 24 | 0x02 | 0x12 | 0x80 | F1 | 28 | O2 sensor (after/downstream of Cat. Conv.), signal incorrect. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 25 | 0x03 | 0x13 | 0x01 | F1 | 29 | O2 sensor heater (before/upstream of Cat. Conv.), heater current (amp too small). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 26 | 0x03 | 0x13 | 0x02 | F1 | 30 | O2 sensor heater (before/upstream of Cat. Conv.), heater current (amp too large). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 27 | 0x03 | 0x13 | 0x04 | F1 | 31 | O2 sensor heater (before/upstream of Cat. Conv.), heater current, short circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 28 | 0x03 | 0x13 | 0x08 | F1 | 32 | O2 sensor heater (after/downstream of Cat. Conv.), heating current (amp too small). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 29 | 0x03 | 0x13 | 0x10 | F1 | 33 | O2 sensor heater (after/downstream of Cat. Conv.), heating current (amp too large). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 30 | 0x03 | 0x13 | 0x20 | F1 | 34 | O2 sensor heater (after/downstream of Cat. Conv.), heating current, short circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 31 | 0x03 | 0x13 | 0x40 | F1 | 35 | Fuel adaptation (lambda control, mixture too lean (rich stop) (Intake air leak, fuel injectors, diaphragm pressure regulator). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 32 | 0x03 | 0x13 | 0x80 | F1 | 36 | Fuel adaptation (lambda control, mixture too rich (lean stop) (Intake air leak, fuel injectors, diaphragm pressure regulator). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 33 | 0x04 | 0x14 | 0x01 | F1 | 37 | Injection valve cylinder 1, short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 34 | 0x04 | 0x14 | 0x02 | F1 | 38 | Injection valve cylinder 1, open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 35 | 0x04 | 0x14 | 0x04 | F1 | 39 | Injection valve cylinder 2, short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 36 | 0x04 | 0x14 | 0x08 | F1 | 40 | Injection valve cylinder 2, open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 37 | 0x04 | 0x14 | 0x10 | F1 | 41 | Injection valve cylinder 3, short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 38 | 0x04 | 0x14 | 0x20 | F1 | 42 | ТInjection valve cylinder 3, open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 39 | 0x04 | 0x14 | 0x40 | F1 | 43 | Injection valve cylinder 4, short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 40 | 0x04 | 0x14 | 0x80 | F1 | 44 | ТInjection valve cylinder 4, open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 41 | 0x05 | 0x15 | 0x01 | F1 | 45 | Injection valve cylinder 5, short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 42 | 0x05 | 0x15 | 0x02 | F1 | 46 | Injection valve cylinder 5, open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 43 | 0x05 | 0x15 | 0x04 | F1 | 47 | njection valve cylinder 6, short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 44 | 0x05 | 0x15 | 0x08 | F1 | 48 | Injection valve cylinder 6, open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 45 | 0x05 | 0x15 | 0x10 | F1 | 49 | Self-adjustment too rich at Idle (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 46 | 0x05 | 0x15 | 0x20 | F1 | 50 | Self-adjustment too lean at Idle (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 47 | 0x05 | 0x15 | 0x40 | F1 | 51 | Self-adjustment too rich at Lower part load (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 48 | 0x05 | 0x15 | 0x80 | F1 | 52 | Self-adjustment too lean at Lower part load (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 49 | 0x06 | 0x16 | 0x01 | F1 | 53 | Self-adjustment too rich at Upper part load (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine) | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 50 | 0x06 | 0x16 | 0x02 | F1 | 54 | Self-adjustment too lean at Upper part load (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 51 | 0x06 | 0x16 | 0x04 | F1 | 61 | Ignition system output stage 3, Cylinder 1 misfires. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 52 | 0x06 | 0x16 | 0x08 | F1 | 62 | Ignition system output stage 3, Cylinder 6 misfires | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 53 | 0x06 | 0x16 | 0x10 | F1 | 63 | Ignition system output stage 3, Current value not reached. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 54 | 0x06 | 0x16 | 0x20 | F1 | 55 | Ignition system output stage 1, Cylinder 2 misfires. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 55 | 0x06 | 0x16 | 0x40 | F1 | 56 | Ignition system output stage 1, Cylinder 5 misfires. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 56 | 0x06 | 0x16 | 0x80 | F1 | 57 | Ignition system output stage 1, Current value not reached. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 57 | 0x07 | 0x17 | 0x01 | F1 | 58 | Ignition system output stage 2, Cylinder 3 misfires. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 58 | 0x07 | 0x17 | 0x02 | F1 | 59 | Ignition system output stage 2, Cylinder 4 misfires | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 59 | 0x07 | 0x17 | 0x04 | F1 | 60 | Ignition system output stage 2, Current value not reached. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 60 | 0x07 | 0x17 | 0x08 | F1 | 64 | Crankshaft signal incorrect. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 61 | 0x07 | 0x17 | 0x10 | F1 | 65 | Crankshaft signal Magnet missing or Number of teeth incorrect. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 62 | 0x07 | 0x17 | 0x20 | F1 | 66 | Crankshaft signal Speed incorrect, too high. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 63 | 0x07 | 0x17 | 0x40 | F1 | 67 | Camshaft signal incorrect/not recognized. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 64 | 0x07 | 0x17 | 0x80 | F1 | 68 | HFM circuit/trimming plug short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 65 | 0x08 | 0x18 | 0x01 | F1 | 69 | HFM circuit/trimming plug open circuit or short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 66 | 0x08 | 0x18 | 0x02 | F1 | 70 | TN speed signal (rpm Output short to ground). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 67 | 0x08 | 0x18 | 0x04 | F1 | 71 | TN speed signal (rpm Output short to positive). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 68 | 0x08 | 0x18 | 0x08 | F1 | 72 | Vehicle speed signal not recognized, short circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 69 | 0x08 | 0x18 | 0x10 | F1 | 73 | Vehicle speed signal implausibly high, short circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 70 | 0x08 | 0x18 | 0x20 | F1 | 74 | PSV relay K3/1 circuit short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 71 | 0x08 | 0x18 | 0x40 | F1 | 75 | Реле подогрева впускного коллектора : Обрыв цепи/короткое замыкание. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 72 | 0x08 | 0x18 | 0x80 | F1 | 76 | Fuel pump relay open circuit or short circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 73 | 0x09 | 0x19 | 0x01 | F1 | 119 | Магнитная муфта механического нагнетателя. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 74 | 0x09 | 0x19 | 0x02 | F1 | 77 | CO potentiometer Input short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 75 | 0x09 | 0x19 | 0x04 | F1 | 79 | Knock sensor 1 signal open circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 76 | 0x09 | 0x19 | 0x08 | F1 | 80 | СKnock sensor 2 signal open circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 77 | 0x09 | 0x19 | 0x10 | F1 | 81 | Ignition timing max retardation reached at least one cylinder. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 78 | 0x09 | 0x19 | 0x20 | F1 | 82 | Ignition angle deviation between the individual cylinders too high. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 79 | 0x09 | 0x19 | 0x40 | F1 | 83 | Knock control analysis, HFM control unit defective | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 80 | 0x09 | 0x19 | 0x80 | F1 | 84 | Short-term self-adjustment Idle/Part-load fault | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 81 | 0x0A | 0x1A | 0x01 | F1 | 85 | Air pump relay-module/switch-valve output, open circuit or short circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 82 | 0x0A | 0x1A | 0x02 | F1 | 239 |  | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 83 | 0x0A | 0x1A | 0x04 | F1 | 240 |  | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 84 | 0x0A | 0x1A | 0x08 | F1 | 86 | Fuel purge switch-valve, open circuit/short circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 85 | 0x0A | 0x1A | 0x10 | F1 | 87 | Fuel purge switch-valve, short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 86 | 0x0A | 0x1A | 0x20 | F1 | 88 | Transmission Upshift delay switchover valve (Y3/3 without function (Logic Chain). | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 87 | 0x0A | 0x1A | 0x40 | F1 | 123 | Контроль воздушной заслонки/Исполнительный механизм воздушного фильтра. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 88 | 0x0A | 0x1A | 0x80 | F1 | 124 | Контроль воздушной заслонки/Исполнительный механизм воздушного фильтра : | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 89 | 0x0B | 0x1B | 0x01 | F1 | 89 | Camshaft timing adjust actuator circuit short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 90 | 0x0B | 0x1B | 0x02 | F1 | 90 | Camshaft timing adjust actuator open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 91 | 0x0B | 0x1B | 0x04 | F1 | 91 | EGR switch-valve short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 92 | 0x0B | 0x1B | 0x08 | F1 | 92 | EGR switch-valve open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 93 | 0x0B | 0x1B | 0x10 | F1 | 93 | Transmission overload protection switch short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 94 | 0x0B | 0x1B | 0x20 | F1 | 94 | Transmission overload protection switch, circuit short or open. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 95 | 0x0B | 0x1B | 0x40 | F1 | 95 | Transmission overload protection switch, circuit short or open. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 96 | 0x0B | 0x1B | 0x80 | F1 | 96 | Transmission overload protection switch signal implausible. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 97 | 0x0C | 0x1C | 0x01 | F1 | 97 | CAN problem Transmission communication from HFM control system faulty. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 98 | 0x0C | 0x1C | 0x02 | F1 | 98 | CAN problem No data reception from ASR. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 99 | 0x0C | 0x1C | 0x04 | F1 | 116 | CAN problem No data reception from IRCL. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 100 | 0x0C | 0x1C | 0x08 | F1 | 243 |  | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 101 | 0x0C | 0x1C | 0x10 | F1 | 99 | CAN problem No data reception from EFP,TPM. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 102 | 0x0C | 0x1C | 0x20 | F1 | 100 | CAN problem No data reception from Diagnosis Module. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 103 | 0x0C | 0x1C | 0x40 | F1 | 244 |  | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 104 | 0x0C | 0x1C | 0x80 | F1 | 117 | Attempt to start with IRCL locked. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 105 | 0x0D | 0x1D | 0x01 | F1 | 101 | No starter signal (Terminal 50), open or short circuit. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 106 | 0x0D | 0x1D | 0x02 | F1 | 102 | Thermocouple CAT B16/6 Temperature too high. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 107 | 0x0D | 0x1D | 0x04 | F1 | 103 | Thermocouple CAT B16/6 Temperature too low. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 108 | 0x0D | 0x1D | 0x08 | F1 | 104 | Fuel safety cut-off settled. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 109 | 0x0D | 0x1D | 0x10 | F1 | 125 | HFM-FSI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 110 | 0x0D | 0x1D | 0x20 | F1 | 105 | Resonance intake manifold switchover valve, short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 111 | 0x0D | 0x1D | 0x40 | F1 | 106 | Resonance intake manifold switchover valve, open circuit/short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 112 | 0x0D | 0x1D | 0x80 | F1 | 107 | Ignition dwell angle control output stage, short to ground | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 113 | 0x0E | 0x1E | 0x01 | F1 | 114 | HFM control unit identification illogical. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 114 | 0x0E | 0x1E | 0x02 | F1 | 108 | Oxygen sensor heater (after/downstream of Cat. Conv.), short to positive. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 115 | 0x0E | 0x1E | 0x04 | F1 | 109 | Oxygen sensor heater (after/downstream of Cat. Conv.), open circuit or short to ground. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 116 | 0x0E | 0x1E | 0x08 | F1 | 115 | НHFM-SFI control unit N3/4 coding bytes illogical | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 117 | 0x0E | 0x1E | 0x10 | F1 | 126 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 118 | 0x0E | 0x1E | 0x20 | F1 | 110 | Voltage supply to HFM-SFI control unit, incorrect | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 119 | 0x0E | 0x1E | 0x40 | F1 | 111 | Voltage supply at HFM-SFI control unit, voltage too low. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 120 | 0x0E | 0x1E | 0x80 | F1 | 112 | HFM control unit faulty | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 121 | 0x0F | 0x1F | 0x01 | F1 | 5 | HFM-SFI, PEC Engine Coolant Temperature Sensor - Intermittent Contact. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 122 | 0x0F | 0x1F | 0x02 | F1 | 8 | Intake Air Temperature Sensor - Intermittent Contact. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 123 | 0x0F | 0x1F | 0x04 | F1 | 13 | Idle speed contact, Loose contact. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 124 | 0x0F | 0x1F | 0x08 | F1 | 16 | Potentiometer throttle valve, Loose contact. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 125 | 0x0F | 0x1F | 0x10 | F1 | 19 | Potentiometer throttle valve drive, Loose contact. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 126 | 0x0F | 0x1F | 0x20 | F1 | 78 | CO potentiometer R33 Loose contact. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 127 | 0x0F | 0x1F | 0x40 | F1 | 247 |  | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 128 | 0x0F | 0x1F | 0x80 | F1 | 127 | ISC и CC/ISC Изношен исполнительный механизм . | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 129 | 0x30 | 0x40 | 0x01 | F1 | 128 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 130 | 0x30 | 0x40 | 0x02 | F1 | 129 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 131 | 0x30 | 0x40 | 0x04 | F1 | 130 | Управление потенциометра. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 132 | 0x30 | 0x40 | 0x08 | F1 | 131 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 133 | 0x30 | 0x40 | 0x10 | F1 | 132 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 134 | 0x30 | 0x40 | 0x20 | F1 | 156 | CAN: signal from ETS/ABS implausible Diagnosis Manual Chassis | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 135 | 0x30 | 0x40 | 0x40 | F1 | 157 | CAN: signal from ETS/ABS implausible Diagnosis Manual Chassis | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 136 | 0x30 | 0x40 | 0x80 | F1 | 160 | CAN: ASR fuel shutoff implausible AD07.51-P-6000-41A | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 137 | 0x31 | 0x41 | 0x01 | F1 | 133 | исполнительный механизм. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 138 | 0x31 | 0x41 | 0x02 | F1 | 134 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 139 | 0x31 | 0x41 | 0x04 | F1 | 135 | Напряжение питания исполнительного мезанизма потенциометра. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 140 | 0x31 | 0x41 | 0x08 | F1 | 161 | CAN: ASR fuel switch-on implausible AD07.51-P-6000-41A | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 141 | 0x31 | 0x41 | 0x10 | F1 | 136 | Управляющее фактическое значение потенциометра. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 142 | 0x31 | 0x41 | 0x20 | F1 | 253 |  | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 143 | 0x31 | 0x41 | 0x40 | F1 | 137 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 144 | 0x31 | 0x41 | 0x80 | F1 | 138 | исполнительный механизм. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 145 | 0x32 | 0x42 | 0x01 | F1 | 139 | Концевой выключатель круиз контроля. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 146 | 0x32 | 0x42 | 0x02 | F1 | 140 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 147 | 0x32 | 0x42 | 0x04 | F1 | 141 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 148 | 0x32 | 0x42 | 0x08 | F1 | 142 | HFM-SFI блок управления. | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 149 | 0x32 | 0x42 | 0x10 | F1 | 143 | Stop lamp switch AD07.51-P-6002-12A | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 150 | 0x32 | 0x42 | 0x20 | F1 | 144 | HFM-SFI control module (N3/4) N3/4 | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 151 | 0x32 | 0x42 | 0x40 | F1 | 254 |  | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | base | 152 | 0x32 | 0x42 | 0x80 | F1 | 255 |  | T_HFM@0x4dcf38[n-1] via FUN_0047f730 |
| HFM | override:flag_o | 51 | 0x06 | 0x16 | 0x04 | F1 | 55 | Ignition system output stage 1, Cylinder 2 misfires. | pairs@0x4dd01c when ident flag (+0x179) == 'o' |
| HFM | override:flag_o | 52 | 0x06 | 0x16 | 0x08 | F1 | 56 | Ignition system output stage 1, Cylinder 5 misfires. | pairs@0x4dd01c when ident flag (+0x179) == 'o' |
| HFM | override:flag_o | 53 | 0x06 | 0x16 | 0x10 | F1 | 57 | Ignition system output stage 1, Current value not reached. | pairs@0x4dd01c when ident flag (+0x179) == 'o' |
| HFM | override:flag_o | 54 | 0x06 | 0x16 | 0x20 | F1 | 58 | Ignition system output stage 2, Cylinder 3 misfires. | pairs@0x4dd01c when ident flag (+0x179) == 'o' |
| HFM | override:flag_o | 55 | 0x06 | 0x16 | 0x40 | F1 | 59 | Ignition system output stage 2, Cylinder 4 misfires | pairs@0x4dd01c when ident flag (+0x179) == 'o' |
| HFM | override:flag_o | 56 | 0x06 | 0x16 | 0x80 | F1 | 60 | Ignition system output stage 2, Current value not reached. | pairs@0x4dd01c when ident flag (+0x179) == 'o' |
| HFM | override:diag49_h_bosch | 35 | 0x04 | 0x14 | 0x04 | F1 | 45 | Injection valve cylinder 5, short to positive. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 36 | 0x04 | 0x14 | 0x08 | F1 | 46 | Injection valve cylinder 5, open circuit or short to ground. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 37 | 0x04 | 0x14 | 0x10 | F1 | 41 | Injection valve cylinder 3, short to positive. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 38 | 0x04 | 0x14 | 0x20 | F1 | 42 | ТInjection valve cylinder 3, open circuit or short to ground. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 39 | 0x04 | 0x14 | 0x40 | F1 | 47 | njection valve cylinder 6, short to positive. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 40 | 0x04 | 0x14 | 0x80 | F1 | 48 | Injection valve cylinder 6, open circuit or short to ground. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 41 | 0x05 | 0x15 | 0x01 | F1 | 39 | Injection valve cylinder 2, short to positive. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 42 | 0x05 | 0x15 | 0x02 | F1 | 40 | Injection valve cylinder 2, open circuit or short to ground. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 43 | 0x05 | 0x15 | 0x04 | F1 | 43 | Injection valve cylinder 4, short to positive. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag49_h_bosch | 44 | 0x05 | 0x15 | 0x08 | F1 | 44 | ТInjection valve cylinder 4, open circuit or short to ground. | pairs@0x4dd004 when diag index == 49 and flag == 'h' and supplier == Bosch |
| HFM | override:diag_ge49 | 70 | 0x08 | 0x18 | 0x20 | F1 | 122 |  | pairs@0x4dcfd0 when diag index >= 49 |
| HFM | override:diag_ge49 | 71 | 0x08 | 0x18 | 0x40 | F1 | 121 |  | pairs@0x4dcfd0 when diag index >= 49 |
| HFM | override:diag_ge49 | 74 | 0x09 | 0x19 | 0x02 | F1 | 120 | ETS сигнал - короткое замыкание на землю или ETS дефект. | pairs@0x4dcfd0 when diag index >= 49 |
| HFM | override:diag_50_55 | 68 | 0x08 | 0x18 | 0x08 | F1 | 145 | CAN: rear axle vehicle speed sensor implausible | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 69 | 0x08 | 0x18 | 0x10 | F1 | 146 | CAN: front axle vehicle speed sensor implausible | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 70 | 0x08 | 0x18 | 0x20 | F1 | 147 | CAN: A/C system pressure implausible Diagnosis Manual Climate Control | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 93 | 0x0B | 0x1B | 0x10 | F1 | 148 | Starter output short circuit to positive | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 94 | 0x0B | 0x1B | 0x20 | F1 | 149 | Starter output short circuit to negative | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 100 | 0x0C | 0x1C | 0x08 | F1 | 150 | CAN: no reception from transmission control module (N15/3) | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 101 | 0x0C | 0x1C | 0x10 | F1 | 151 | CAN: no reception from air conditioning/automatic air conditioning (N19,N22) | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 102 | 0x0C | 0x1C | 0x20 | F1 | 159 | CAN: kno reception from EIS AD07.51-P-6000-41A | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 103 | 0x0C | 0x1C | 0x40 | F1 | 152 | CAN: no reception from instrument cluster (A1) AD07.51-P-6000-41A | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 114 | 0x0E | 0x1E | 0x02 | F1 | 153 | Electric suction-type fan (engine/AC) (M4/3) output - short circuit to positive | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 115 | 0x0E | 0x1E | 0x04 | F1 | 154 | Electric suction-type fan (engine/AC) (M4/3) output - short circuit to negative | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 127 | 0x0F | 0x1F | 0x40 | F1 | 155 | Transmission version coding implausible Perform version coding | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 134 | 0x30 | 0x40 | 0x20 | F1 | 156 | CAN: signal from ETS/ABS implausible Diagnosis Manual Chassis | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 135 | 0x30 | 0x40 | 0x40 | F1 | 157 | CAN: signal from ETS/ABS implausible Diagnosis Manual Chassis | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 136 | 0x30 | 0x40 | 0x80 | F1 | 160 | CAN: ASR fuel shutoff implausible AD07.51-P-6000-41A | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 140 | 0x31 | 0x41 | 0x08 | F1 | 161 | CAN: ASR fuel switch-on implausible AD07.51-P-6000-41A | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 148 | 0x32 | 0x42 | 0x08 | F1 | 162 | CAN: cruise control signal from EIS implausible AD07.51-P-6002-08A | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 149 | 0x32 | 0x42 | 0x10 | F1 | 158 | CAN: brake information implausible Diagnosis Manual Chassis | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 151 | 0x32 | 0x42 | 0x40 | F1 | 163 | CAN: signal from cruise control pushbutton missing | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| HFM | override:diag_50_55 | 152 | 0x32 | 0x42 | 0x80 | F1 | 164 | CAN: signal from cruise control pushbutton implausible | pairs@0x4dcfd8 when 50 <= diag index <= 55 |
| PMS | base | 1 | 0x00 | 0x10 | 0x01 | F1 | 2 | Coolant temperature sensor, short circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 2 | 0x00 | 0x10 | 0x02 | F1 | 3 | Coolant temperature sensor, open circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 3 | 0x00 | 0x10 | 0x04 | F1 | 4 | Coolant temperature sensor, incorrect | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 4 | 0x00 | 0x10 | 0x08 | F1 | 6 | Intake air temperature sensor, short circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 5 | 0x00 | 0x10 | 0x10 | F1 | 7 | Intake air temperature sensor, open circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 6 | 0x00 | 0x10 | 0x20 | F1 | 9 | PMS Control unit, Intake manifold pressure implausible | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 7 | 0x00 | 0x10 | 0x40 | F1 | 10 | PMS Control unit, No Intake manifold pressure | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 8 | 0x00 | 0x10 | 0x80 | F1 | 11 | Idle speed contact closed signal incorrect | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 9 | 0x01 | 0x11 | 0x01 | F1 | 68 | Контакт холостого хода. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 12 | 0x01 | 0x11 | 0x08 | F1 | 13 | Potentiometer throttle valve, value too high | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 13 | 0x01 | 0x11 | 0x10 | F1 | 14 | Potentiometer throttle valve, value too low | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 14 | 0x01 | 0x11 | 0x20 | F1 | 16 | Potentiometer throttle valve drive value too high/incorrect | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 15 | 0x01 | 0x11 | 0x40 | F1 | 17 | Potentiometer throttle valve drive value too low/incorrect | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 16 | 0x01 | 0x11 | 0x80 | F1 | 19 | Idle speed control at lower control stop area, malfunction | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 17 | 0x02 | 0x12 | 0x01 | F1 | 20 | Idle speed control at upper control stop area, malfunction | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 18 | 0x02 | 0x12 | 0x02 | F1 | 21 | Idle speed control in limp home-mode (emergency operation) | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 19 | 0x02 | 0x12 | 0x04 | F1 | 22 | O2 sensor voltage too large | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 20 | 0x02 | 0x12 | 0x08 | F1 | 23 | O2 sensor, open circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 21 | 0x02 | 0x12 | 0x10 | F1 | 24 | O2 sensor signal illogical | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 22 | 0x02 | 0x12 | 0x20 | F1 | 69 | Рециркуляция выхлопных газов : короткое замыкание на +12V. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 23 | 0x02 | 0x12 | 0x40 | F1 | 70 | Рециркуляция выхлопных газов : обрыв цепи / короткое замыкание на массу.. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 25 | 0x03 | 0x13 | 0x01 | F1 | 25 | O2 sensor heater current (amps) too small | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 26 | 0x03 | 0x13 | 0x02 | F1 | 26 | O2 sensor heater current (amps) too large | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 27 | 0x03 | 0x13 | 0x04 | F1 | 27 | O2 sensor heater, short circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 31 | 0x03 | 0x13 | 0x40 | F1 | 28 | Fuel adaptation (lambda) control mixture too lean (Intake air leak, fuelinjectors, diaphragm pressure regulator) | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 32 | 0x03 | 0x13 | 0x80 | F1 | 29 | Fuel adaptation (lambda) control mixture too rich (Intake air leak, fuelinjectors, diaphragm pressure regulator) | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 33 | 0x04 | 0x14 | 0x01 | F1 | 30 | Injection valve cylinder 1/4 short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 34 | 0x04 | 0x14 | 0x02 | F1 | 31 | Injection valve cylinder 1/4 open circuit or short to ground | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 35 | 0x04 | 0x14 | 0x04 | F1 | 32 | Injection valve cylinder 2/3 short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 36 | 0x04 | 0x14 | 0x08 | F1 | 33 | Injection valve cylinder 2/3 open circuit or short to ground | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 37 | 0x04 | 0x14 | 0x10 | F1 | 64 | Дефект сигнала иммобилайзера от блока управления EZL. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 38 | 0x04 | 0x14 | 0x20 | F1 | 65 | Дефект сигнала иммобилайзера от блока управления EZL. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 39 | 0x04 | 0x14 | 0x40 | F1 | 66 | Попытка запуска двигателя с заблокированым блоком IFZ. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 40 | 0x04 | 0x14 | 0x80 | F1 | 67 | Сигнал иммобилайзера от блока IFZ не правдоподобный. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 45 | 0x05 | 0x15 | 0x10 | F1 | 34 | Self-adjustment too rich at Idle (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine) | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 46 | 0x05 | 0x15 | 0x20 | F1 | 35 | Self-adjustment too lean at Idle (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine) | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 47 | 0x05 | 0x15 | 0x40 | F1 | 36 | Self-adjustment too rich at Part load (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine) | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 48 | 0x05 | 0x15 | 0x80 | F1 | 37 | Self-adjustment too lean at Part load (Intake air leak, fuel injectors, diaphragm pressure regulator, wear engine) | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 51 | 0x06 | 0x16 | 0x04 | F1 | 38 | Ignition system output stage 1, short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 52 | 0x06 | 0x16 | 0x08 | F1 | 39 | Ignition system output stage 1, Cylinder 1/4 misfires | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 53 | 0x06 | 0x16 | 0x10 | F1 | 40 | Ignition system output stage 1, Amperage not achieved | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 54 | 0x06 | 0x16 | 0x20 | F1 | 41 | Ignition system output stage 2, short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 55 | 0x06 | 0x16 | 0x40 | F1 | 42 | Ignition system output stage 2, Cylinder 2/3 misfires | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 56 | 0x06 | 0x16 | 0x80 | F1 | 43 | Ignition system output stage 2, Amperage not achieved | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 60 | 0x07 | 0x17 | 0x08 | F1 | 44 | Crankshaft signal incorrect | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 61 | 0x07 | 0x17 | 0x10 | F1 | 45 | Crankshaft signal Magnet missing or Numbers of teeth incorrect | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 62 | 0x07 | 0x17 | 0x20 | F1 | 46 | Crankshaft signal Speed incorrect, too high | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 64 | 0x07 | 0x17 | 0x80 | F1 | 47 | PMS circuit/trimming plug short to ground | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 65 | 0x08 | 0x18 | 0x01 | F1 | 48 | PMS circuit/trimming plug open circuit or short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 66 | 0x08 | 0x18 | 0x02 | F1 | 49 | TN speed signal (rpm) Output short to ground | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 67 | 0x08 | 0x18 | 0x04 | F1 | 50 | TN speed signal (rpm) Output short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 68 | 0x08 | 0x18 | 0x08 | F1 | 51 | Vehicle speed signal not recognized, short circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 69 | 0x08 | 0x18 | 0x10 | F1 | 52 | Vehicle speed signal too high, short circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 70 | 0x08 | 0x18 | 0x20 | F1 | 53 | PSV relay open circuit or short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 71 | 0x08 | 0x18 | 0x40 | F1 | 54 | PSV relay short to ground | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 72 | 0x08 | 0x18 | 0x80 | F1 | 55 | Fuel pump relay open circuit or short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 73 | 0x09 | 0x19 | 0x01 | F1 | 56 | Fuel pump relay short to ground | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 74 | 0x09 | 0x19 | 0x02 | F1 | 57 | CO potentiometer Input circuit short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 80 | 0x09 | 0x19 | 0x80 | F1 | 61 | Кратковременная регулировка хол. хода/частичная нагрузка. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 81 | 0x0A | 0x1A | 0x01 | F1 | 71 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 82 | 0x0A | 0x1A | 0x02 | F1 | 72 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 84 | 0x0A | 0x1A | 0x08 | F1 | 59 | Fuel purge switch-valve open circuit or short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 85 | 0x0A | 0x1A | 0x10 | F1 | 60 | Fuel purge switch-valve short to ground | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 86 | 0x0A | 0x1A | 0x20 | F1 | 62 | Transmission shifting delay/smooth switch-valve, open circuit or short circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 93 | 0x0B | 0x1B | 0x10 | F1 | 73 | Transmission protection short to ground or active too long | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 96 | 0x0B | 0x1B | 0x80 | F1 | 74 | Transmission protection open circuit or short to positive | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 97 | 0x0C | 0x1C | 0x01 | F1 | 75 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 98 | 0x0C | 0x1C | 0x02 | F1 | 76 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 100 | 0x0C | 0x1C | 0x08 | F1 | 78 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 101 | 0x0C | 0x1C | 0x10 | F1 | 79 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 119 | 0x0E | 0x1E | 0x40 | F1 | 63 | Напряжение питания на блоке управления PMS : | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 121 | 0x0F | 0x1F | 0x01 | F1 | 5 | PMS Датчик температуры двигателя : плохой контакт. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 122 | 0x0F | 0x1F | 0x02 | F1 | 8 | Датчик температуры впускного воздуха : плохой контакт. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 123 | 0x0F | 0x1F | 0x04 | F1 | 12 | Idle speed contact open circuit | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 124 | 0x0F | 0x1F | 0x08 | F1 | 15 | Potentiometer throttle valve, Loose contact | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 125 | 0x0F | 0x1F | 0x10 | F1 | 18 | Управляющее значение потенциометра в исполнительном механизме хол. хода. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 126 | 0x0F | 0x1F | 0x20 | F1 | 58 | CO потенциометр: плохой контакт. | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 127 | 0x0F | 0x1F | 0x40 | F1 | 77 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 128 | 0x0F | 0x1F | 0x80 | F1 | 80 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |
| PMS | base | 129 | 0x30 | 0x40 | 0x01 | F1 | 141 |  | T_PMS@0x4dceb4[n] via FUN_0047f730 |

## 16. Идентификация и совместимость M104/M111

HFMreader: `F0` сканирует 00–1B и получает 28 байт; первые 2 — manufacturer, 2–11 — строка идентификатора, 12–21 — десятизначный номер, 22–25 — аппаратная/ПО дата, 26–27 — индекс диагностики. HFMScan, согласно отчёту, читает до 0x27, пропуская 0x1C–0x25; последние байты могут задавать страницу таблицы дескрипторов. Эти диапазоны нельзя слить в один жёсткий способ идентификации без проверки.
ЭБУ `0135451332` с diag `41/00` автор HFMreader отмечает как валидированный для отдельных операций управления. Это **не автоматическое разрешение** применять их на всех M104/M111.
### 16.1 Группы и режимы опроса HFMreader

| Имя | Префикс | ACK | Селекторы | Параметры |
| --- | --- | --- | --- | --- |
| rpm_low | 20 | DF | ["54"] | [] |
| rpm_high_context | 20 | DF | ["55","C4","B9"] | ["ignition_angle"] |
| mixture_iat | 20 | DF | ["23","99","68"] | ["o2_sensor_voltage"] |
| airflow | 20 | DF | ["49","BD","B7","C2","C4","B9"] | ["throttle_angle","engine_speed","engine_load","ignition_angle"] |
| coolant | 21 | DE | ["D7","BF","B7","BE","69","67"] | ["coolant_temperature","intake_air_temperature"] |
| air_mass_low_candidate | 20 | DF | ["48"] | ["air_mass"] |
| air_mass_high_candidate | 20 | DF | ["49"] | ["air_mass"] |

| Профиль | Группы опроса |
| --- | --- |
| rpm | airflow |
| full7 | rpm_low, rpm_high_context, mixture_iat, airflow, coolant |
| full8 | rpm_low, rpm_high_context, mixture_iat, airflow, coolant, air_mass_low_candidate, air_mass_high_candidate |
| validation | rpm_low, rpm_high_context, mixture_iat, airflow, coolant, air_mass_low_candidate, air_mass_high_candidate |

### 16.2 Конфигурация ЭБУ

В экспериментальном HFMreader присутствует чтение `27 02 24 F0`: результирующие байты `c` (кодировка) и `m` (маска неприменимости). Восстановленные правила описаны в `R0/protocol/protocol.md §8`: КПП и 4/5 ступеней, рынок USA, катализатор, признак диагностического модуля, обучение упоров дросселя и определение номера HHT-экрана 103/104/107/108/109. Эти правила имеют статус условных и должны быть подтверждены на реальном блоке.
## 17. Стирание DTC, адаптации и тесты исполнительных устройств

**ОПАСНОСТЬ ИЗМЕНЕНИЯ СОСТОЯНИЯ ЭБУ.** В таблицах приведены алгоритмы, найденные в статическом анализе, чтобы документировать программу, но не как инструкция выполнять их на подключённой машине. Все функции записи по умолчанию должны быть disabled в производственном драйвере.
### 17.1 Стирание памяти неисправностей

HFMScan: `F3 FF` с 200/40/500-мс паузами, повтор два раза, предварительно `10 00`, затем reread (по `commands.json`). HHT-WIN: AT-шаблон с фрагментом `F3 01 FF 00`, который является **внутренним форматом шаблона**, не прямым UART дампом. HFMreader: требование валидированного номера блока/диаг.индекса и чтения снимка ошибок; более подробная последовательность приведена в R0:
```text
PRECHECK: read identification; save raw DTC bank0 + bank1; user authorization
200 ms
CMD 10 / ARG 00; 200 ms
CMD F3 / ARG FF;  50 ms
CMD 10 / ARG 00; 500 ms
CMD 10 / ARG 00; 200 ms
CMD F3 / ARG FF;  50 ms
CMD 10 / ARG 00; 500 ms
CMD 10 / ARG 00
POSTCHECK: re-read banks and compare, persist before/after
```

При промежуточном тайм-ауте команда может быть выполнена частично: автоматическое повторение опасно. Наличие ACK не доказывает успешное стирание.
### 17.2 Сброс адаптаций HFMreader — восстановленный экспериментальный сценарий

Условия по коду: зажигание включено, двигатель заглушён, подтверждённая идентификация ЭБУ, свежие DTC и снимок данных, время подтверждения не более 20 секунд. Последовательность (каждая команда нуждается в подтверждении):
`CF* FF → C1* FC → E0* 00 → CF* 00 → C0* FC` (с проверкой возврата `00`, `03` на входе). Требуется цикл зажигания. Не подтверждено реальным ЭБУ и последующей адаптацией.
### 17.3 Исполнительный тест продувки адсорбера

В коде предусмотрен тест клапана продувки при 400–1500 об/мин, 80–110 °C, с предварительным входом в управляющий режим `CF* FF C1* FC`, проверкой селектора `96` и управлением `D4* 1A` (ON) / `D4* 00` (OFF). Встроен таймер 30 секунд и сценарий restore, связанный с `D0..D4`. Его существование в HFMreader **не доказывает** применимость к другим ревизиям; использовать только на отдельном стенде после верификации.
### 17.4 Прочие функции — статус подтверждения

| Функция | Найдено | Что достоверно | Что НЕ УСТАНОВЛЕНО | Действие для ESP32 |
| --- | --- | --- | --- | --- |
| Сброс DTC | HFMreader/HFMScan/HHT-WIN | Команда F3 FF и исходные последовательности | реальный эффект у иных ЭБУ | запретить до стенда |
| Сброс топливных адаптаций | HFMreader | Код условного сценария и идентификационные проверки | область поддержки M111/других M104 | только исследовательский режим |
| Обучение дросселя | HFMScan UI mnuAdaptThrottleClick 0x4D835C | Функция меню есть | валидированная последовательность байтов, исходные условия | НЕ РЕАЛИЗОВЫВАТЬ |
| Кодировка блока | HFMScan mnuSetCodeNumberClick 0x4D2754 | Найдена функция | смысл и формат записи | НЕ РЕАЛИЗОВЫВАТЬ |
| Ввод в эксплуатацию | отдельные HHT сценарии возможны | нет доказанной единой команды | алгоритм commissioning, условия, ECU-supported | НЕ РЕАЛИЗОВЫВАТЬ |
| Продувка адсорбера | HFMreader hht_control.session | Команды D4 и ограничения программой | успешное испытание на блоке | выключено по умолчанию |
| Форсунки/реле/EGR/фазорегулятор | HHT-WIN содержит строки и экраны | наличие UI и идентификаторов | точные команды активации для конкретного ЭБУ | НЕ РЕАЛИЗОВЫВАТЬ |
| Номер VIN | HFMScan F9 00–0D | Статически есть чтение | поддержка на всех M104/M111 | только read с feature check |

## 18. Обработка исключений, ограничения, защита двигателя

| Сбой | Признак | Действие |
| --- | --- | --- |
| Нет связи | AA/55 не совпадает | разорвать сессию, журналировать питание/VBUS/BREAK, затем ограниченный retry чтения |
| Эхо неверно | RX_echo != TX | записать сырые байты, отменить текущую транзакцию |
| ACK неверен | ACK != (CMD ^ FF) | сбросить контекст, не декодировать DATA |
| Тайм-аут | не получены 2 байта в 750 мс | записать partial RX, отметить stale, безопасная реинициализация |
| Несоответствие ECU | нет идентификатора или неподдерживаемый diag index | только raw mode, опасные команды заблокированы |
| Неверная длина Live Data | buffer shorter than declared | не рассчитывать параметр и не отображать как достоверный |
| Ошибочное значение | нет данных, замещающее значение, RPM=0 gate | отметить недостоверность/условность по описанию |
| Неудачная операция записи | частичный/неизвестный результат | никакого автоматического повтора, диагностика только после разрешения неопределённости |

## 19. Архитектура ESP32-S3 и приёмочные испытания

Проектировать четыре независимых уровня: `FTDI USB Host (VID/PID, bulk + control)` → `HFM nine-bit transport` → `ECU profile / command catalog / parser` → `DTC & live engine / WebSocket / storage`. WebUI не должен содержать внутренних команд и OEM-формул, если планируется аппаратная защита прошивки. Клиенту отдавать только значения/ошибки и статусы подтверждения.
**Порядок испытаний:** (1) USB enumeration на FT232RL без авто; (2) FTDI VCP vendor requests и чтение статуса; (3) проверка BREAK/паритета на низковольтном стенде; (4) аппаратный эмулятор HFM, ответы `AA55`, эхо/ACK/timeout; (5) идентификация реального ЭБУ M104 и M111; (6) оба диапазона F1 с искусственно известной неисправностью; (7) RPM/температуры/MAF с сопоставлением HHT; (8) только после этого — отдельный стенд записи/исполнительных тестов.
USB Pcap (Windows) и логический анализатор на гальванически/по напряжению согласованной K-Line нужны для разведения задержек драйвера FT232, фактических стартовых битов и логики программы. Факт сохранения USB-пакета не подтверждает 9-й бит на физическом проводе.
## 20. Противоречия и невосстановленные функции

| ID | Неоднозначность | Источники | Практический риск/критерий разрешения |
| --- | --- | --- | --- |
| C-01 | Банки F1 00–0F и 10–1F swapped | HFMreader vs HFMScan; HHT templates | не выдавать current/stored без реальной проверки |
| C-02 | Доступность DTC позиций 129–152 | HFMreader 16B банк против HFMScan расширенных адресов | проверить F1 30–32/40–42 на известном варианте и только безопасно |
| C-03 | RPM RAW ×25 против raw/4 | разные внутренние представления HFMreader/HFMScan hfmobd.ini | сопоставить фактический F1/HFM DATA со значением HHT |
| C-04 | O2 ×4 мВ против 5.1102*raw−192.83 мВ | HFMreader native и HHT экспериментальная | разные селекторы/offset; проверить экран и напряжение |
| C-05 | Входы MAF и \`B7\` gate | HFMreader кандидаты, HFMScan, HHT-WIN | одновременно логировать оба байта и RPM |
| C-06 | Нормализация 9-го бита и RX parity | Windows KKL показывает байты без физического parity provenance | наблюдать K-Line при передаче и адаптере |
| C-07 | HHT V/B и пороги diag-index | HHTWin tables, HFMreader legacy/modern | определить вариант перед выбором таблиц |
| C-08 | Версия M111 | отсутствуют реальные контрольные трассы разных Bosch | требуется ЭБУ M111 минимум одной ревизии |
| C-09 | Декодирование commissioning/адаптаций и других актуаторов | UI строки HFMScan и HHT | не применять без полного пути TX и ACK |
| C-10 | Внутренние HHT-шаблоны не есть UART | sg_table_c814.json, HFMWIN.DLL 16-bit | разобрать executor HHTWINX и сравнить USBPcap |
| C-11 | Таблицы температур не обязательно идентичны | HHT 256 coolant, HFMreader 256, HFMScan 14-точечная | проверить все 256 значений и диагностический индекс |

## 21. Контроль доказательств и реестр источников

### 21.1 Файлы-первоисточники и контрольные суммы
Контрольная сумма на уровне архивного входа:
- `HFMreader_REPORT_0(1).zip` — SHA-256 `1d2b4ef525d8cbe4dff254686817c990839f6693dc353ced35ed240151c97928`
- `HFMreader_REPORT_1.zip` — SHA-256 `07ac3849f0a0edd9a214d903562afaf4dc092f494310f8344a972dc85f234374`
- `HFMreader_REPORT_2.zip` — SHA-256 `4dceff145ef1285710629251981c58edea540c72704cb5d7065116dbd95b17ff`
- `HFMreader_REPORT_3.zip` — SHA-256 `1083a69b9204eb0acd84ed3c8954fcf0d95a2b4dfb4e2a7aab4ecef1f265e43b`
- `HFMreader_REPORT_4.zip` — SHA-256 `f3e49b64426fb7566910d828fd11d99c55885f6e99642d9d7e6f7111c6d55b25`
- `HFMScan_REPORT.zip` — SHA-256 `48223e3b16c2ca03ae2ec599a17cabb6f2391c0e201fb41296ff0aa9ce77df2d`
- `HHTwin_REPORT.zip` — SHA-256 `e6f0a9bca1c3f6f304d1277590a388de3424e1055d3a010f85a8a79a0c6e41df`

Ниже перечислены важнейшие экспортные файлы, используемые для построения этого Markdown. Относительный путь указан от раскрытого архива (то есть от корня `HFMreader_REPORT_0`, `HFMScan_REPORT`, `HHTwin_REPORT`).
| Файл (из распакованного ZIP) | Байт | SHA-256 |
| --- | --- | --- |
| HFMreader_REPORT_0_copy1/HFMreader_REPORT_0/protocol/protocol.md | 28094 | cd8e1834c8c983e79f4bad1e18f980592c6086cb24d863ec526025e25b9e3e53 |
| HFMreader_REPORT_0_copy1/HFMreader_REPORT_0/exports/commands.json | 36442 | 48995b1edd821c0542d6de0bfef9feb633d25bd2030b23dc001d741e8d4bd303 |
| HFMreader_REPORT_0_copy1/HFMreader_REPORT_0/exports/formulas.json | 16524 | 9c67e2f2c640c2b3eb26b78c54436af708b87289eec50ef489828f49dc0fcfa3 |
| HFMreader_REPORT_0_copy1/HFMreader_REPORT_0/exports/live_data.csv | 15851 | 124277d08e24e4608290cce20b6811f5478d36cdb0cf630b1ddec2684cc31211 |
| HFMreader_REPORT_0_copy1/HFMreader_REPORT_0/live_data/hht_recipes.csv | 9883 | f3e91092485ffd25e08a5b2d81598c48ed285b4753b1cac0915aa80ce05c277d |
| HFMreader_REPORT_0_copy1/HFMreader_REPORT_0/dtc/dtc.csv | 59926 | 26e5b32d7d2c5d6a4d5582352d84876913ab99ce5b370f6653c579722fea282f |
| HFMreader_REPORT_0_copy1/HFMreader_REPORT_0/dtc/bit_map.csv | 39034 | 50ee4e2c6833f159d7ed320e3af13991a1a6a013e4e3ca2a5ad7312fd8eea958 |
| HFMreader_REPORT_0_copy1/HFMreader_REPORT_0/dtc/variant_remaps.json | 3243 | 19cc074d78caa1d9f1017189068dc7d26e1bb389807dd5d8220befb6921c32bb |
| HFMScan_REPORT/HFMScan_REPORT/protocol/commands.json | 6704 | 063569c49915ac004fcced998e7359fe51ecc216b0424636063377b4e8161474 |
| HFMScan_REPORT/HFMScan_REPORT/reports/DTC_REPORT_RU.md | 52104 | 308396e0e1b17bc2481abcd10d0b91896e2c555b37446c0b23c52e14c126caa4 |
| HFMScan_REPORT/HFMScan_REPORT/dtc/dtc.csv | 104311 | 3b30d0eb0b8a56b0877bfc9b435420d3de0d4f4ed29b04d5ac0ec062bf4575ad |
| HFMScan_REPORT/HFMScan_REPORT/dtc/dtc_bitmaps.json | 3922 | d0d58064ffecf9328b5dd6f5bca2b6fe5f65e698568419856dca8073a663e563 |
| HHTwin_REPORT/HHTwin_REPORT/protocol/sg_table_c814.json | 26437 | cf8904bd4ea6dd22d69a457c391c08474981376fe2370a2daa443d96f5a1b8a2 |
| HHTwin_REPORT/HHTwin_REPORT/dtc/dtc_bit_tables.json | 126836 | 9fa53e0e6ea4127edaa51921d3397712c5c9604a5621d190d3f0af4c4af44272 |
| HHTwin_REPORT/HHTwin_REPORT/dtc/fault_text_records.json | 110809 | 73ac471472ec86e15f3885dd01d19204b6a83982c148a44fc24b7cbaa8b88030 |
| HHTwin_REPORT/HHTwin_REPORT/work/istwert_records.json | 50149 | a855f8467858a706b5525901d4a86ef5a3cc3a8a1a84eb9d17576762cce46cf8 |
| HHTwin_REPORT/HHTwin_REPORT/work/conv_inventory.json | 18453 | 9a7ceda86766982305e07fe369d2a8d094b79f44f4f00cb70e250c011f1a7e2a |
| HHTwin_REPORT/HHTwin_REPORT/work/hfm_tables.json | 91288 | 84c4fa6130cbb9b7ca322042971e35268b9a4576dc2981a7e1a8805f6b5cf753 |
| HFMreader_REPORT_1/HFMreader_REPORT_1/extracted/release_zip/HFMreader_1.1.1-public.1_WIN64/_internal/hfmreader/hht_experimental/candidates.json | 52856 | 57bef9ad06f4021e3fb80d1570c911bd865fd24bc3879a924f0fd081437abde6 |

### 21.2 Метод проверки полноты
Программно проверено: число 29 HHT-рецептов, 152 позиций базовой DTC-карты, 163 строк каталога HFMreader, 7 заводских таблиц HHT, 242 заводских текстовых правил, 173 экранных HHT записи, 115 AT-телеграмм и 256 элементов каждой из двух LUT. Контроль единственности и полноты относится к предоставленным файлам, а не ко всем возможным HFM-SFI прошивкам Mercedes.
### 21.3 Что отсутствует для окончательной спецификации
Нет новых USBPcap/PulseView трасс с реального M104 и M111, нет подтверждённых результатами автомобиля тестов сброса и актуаторов, нет полного набора ECU.bin/ecu.dat для разных диагностических индексов. Поэтому утверждение «полный протокол для всех Bosch HFM» делать нельзя: это максимальная консолидированная **статическая** спецификация из предоставленного набора.
---
**Конец сводного технического документа.**
