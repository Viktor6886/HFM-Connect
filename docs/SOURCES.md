# Реестр источников

Индексы используются в коде (`HfmProfiles.cpp`), документации и WebUI. Заводские документы
Mercedes-Benz и сторонние руководства **не публикуются** в репозитории (авторские права);
они хранятся локально у владельца проекта в `HFM-Connect/Documents/`. Собственные
спецификации проекта лежат в `docs/specs/`.

## Спецификации проекта (в репозитории, `docs/specs/`)

| Индекс | Файл | Назначение |
|---|---|---|
| R11 | HFM_SFI_M104_M111_Protocol_Revised_1_1.pdf (357 с.) | Базовый протокол REV 1.1: команды, формулы, DTC-карты |
| R13 | HFM_SCAN_Protocol_Addendum_REV1_3_Consolidated.docx | Дополнение 1.3 |
| R14 | HFM_SCAN_Protocol_Addendum_REV1_4_Verified.docx | Дополнение 1.4 — **высший приоритет** при расхождениях |
| R02 | HFM_SFI_Official_PDF_Research_and_Requirements.pdf | Вторичный реестр 13 материалов |
| RG | HFM_PROTOCOL_FULL_GPT.md | Сводный рабочий документ |

## Данные реверс-анализа (в репозитории, `data/hfmreader/`)

| Индекс | Файл | Содержание |
|---|---|---|
| HR | protocol.md, commands.json | Протокол HFMreader 1.1.1 (EXE SHA-256 b5aa7e0e…a2ed) |
| HR-DTC | dtc.csv, bit_map.csv, variant_remaps.json | 163 кода, 152 позиции, ремапы |
| HR-LIVE | live_data.csv, formulas.json, hht_recipes.csv, lookup_tables.json | Параметры, формулы, 29 HHT-рецептов, таблицы |
| HR-TV | test_vectors.json | Тестовые векторы (SYNTH_SIM / FORMULA) |

Отчёты HFMScan (Delphi, Ghidra) и HHTwin — локально в `Protocol view/`; использованы для
сверки (keep-alive 10 00/1300 мс, F9 VIN, F1 30–32/40–42, противоположная трактовка банков).

## Заводские и сторонние документы (локально, `Documents/`)

| Индекс | Документ | Что подтверждает |
|---|---|---|
| I03 | 2147.pdf — MBNA 07.5/1, 04.1994 | Переход на кодируемые ЭБУ 01–03/1994, 9-значный код, HHT меню 6, лимит 3200 об/мин |
| I04 | 659.pdf — MBUSA P-SI-14.00/14, 02.2001 | MIL горит при подключённом приборе; перечень моделей 1993–1996 HFM/LH |
| I05 | 24072001.pdf — W124 ETM 124.07-2.00/I | N3/4, X11/4, K1/2, цепи 15/30 |
| I06 | 24072002.pdf — W124 ETM 124.07-2.00/II | Датчики, форсунки, варианты до/после 09.1994 |
| I07 | Test and Adjustment Data-All HFM-SFI Engines.pdf (38 с.) | Нормы M104/M111 по датам и моделям |
| I08 | Engine Test, Adjustment.pdf (HFM с.49–56) | Разъёмы, методы проверки |
| I09 | Engine 104 HFM-SFI in Models 129 … Engine 111 in Model 202.pdf | DM 8.5, X11/22, P-коды |
| I10 | Engine 104 HFM-SFI in Model 129 as of MY1996, 140, 210.pdf | DM 8.6 |
| E111 | Engine 111.pdf (OEM 11/1–11/19) | Память DTC, импульсные процедуры, 38-конт. W202, DAS 40 запусков |
| WIS | Описание по годам.docx (GF07.51-P-3017D/4103D) | Перечень двигателей HFM-SFI, логика памяти, 19 поездок |
| IRCL | IRCL_W210.072_1996_DAS2.pdf | Неизменяемая идентификация DAS 2 |
| SNAP | DTC_Connector_Pinouts_38-16-8.pdf | 38-конт. распиновка (сторонний) |
| TRISCO | Trisco_Palm_Scan_Manual.pdf | Меню стороннего сканера (пример ME, не HFM) |
| IMP | _Instructions_impulse_counter_*.pdf | Импульсный считыватель (сторонний) |
| ETA | M104_ETA_rewiring.pdf | Ремонт жгута ETA (сторонний) |
| DTCL | DTC_*.pdf, Mercedes-OBD1-Fault-Codes*.pdf, mb_dtc_adapt1.pdf | Списки кодов (сторонние) |

Правило приоритета (REV 1.4 §47): заводской документ конкретной модификации > заводской
документ семейства > бюллетень > проверенная трасса (формат обмена) > сторонние
руководства > реконструкция из ПО.
