# OpenTitan и Zephyr RTOS

Практические работы по курсу «Аппаратная безопасность»: приложения Zephyr RTOS выполняются в RTL-модели аппаратного корня доверия OpenTitan Earl Grey, которую строит Verilator.

Выполнили: Груздев И., Гошин А., группа ИУ10-95.

## Практические работы

| № | Работа | Результат |
| :---: | --- | --- |
| 01 | [Подготовка окружения и запуск Zephyr](labs/01-environment/README.md) | Стенд на Apple Silicon (lima + x86-контейнер через Rosetta); UART smoke test — `PASSED`; Zephyr `Hello World!` |
| 02 | [UART-сервис эхо-повтора](labs/02-uart-echo/README.md) | Эхо без потерь (115 байт, сверено счётчиком); строка длиннее буфера; команда `EXIT` → `OK:BYE`; Python-клиент — `ALL TESTS PASSED` |

## Окружение

| Компонент | Версия |
| --- | --- |
| Хост | macOS 15, Apple Silicon |
| Виртуальная машина | lima 2.2.1, Ubuntu 24.04 arm64 |
| Рабочее окружение | Docker, Ubuntu 24.04 x86_64 через Rosetta 2 |
| OpenTitan | `earlgrey_1.0.0` (`4aacad7540`) |
| Verilator | 4.210 |
| Zephyr RTOS | 3.7.0 |
| Zephyr SDK | 0.16.8 |
| Плата Zephyr | `opentitan_earlgrey` |
