#!/usr/bin/env python3
"""ЛР2. Хост-клиент для проверки UART-эхо-сервиса в модели OpenTitan.

Использование: python3 host_client.py /dev/pts/N
"""
import sys
import time

import serial

TIMEOUT = 60.0  # секунд на один тест: модель под Rosetta работает медленно

# (название, что отправить); эхо должно совпасть байт в байт
ECHO_TESTS = [
    ("короткая строка", b"HELLO OPENTITAN ZEPHYR\r\n"),
    ("строка длиннее буфера команды", b"0123456789" * 8 + b"\n"),
    ("exit строчными не завершает сервис", b"exit\n"),
]


def read_until(ser, done, timeout=TIMEOUT):
    """Читает порт, пока done(данные) не вернёт True или не выйдет время."""
    data = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data += ser.read(ser.in_waiting or 1)
        if done(data):
            break
    return bytes(data)


def main():
    if len(sys.argv) != 2:
        print(f"Использование: {sys.argv[0]} /dev/pts/N")
        return 1

    ser = serial.Serial(sys.argv[1], baudrate=115200, timeout=0.2)
    ser.reset_input_buffer()  # отбросить то, что сервис напечатал при старте
    failed = 0

    for name, tx in ECHO_TESTS:
        start = time.monotonic()
        ser.write(tx)
        rx = read_until(ser, lambda d: len(d) >= len(tx))
        ok = rx == tx
        failed += not ok
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: "
              f"{len(tx)} байт, {time.monotonic() - start:.1f} с")
        if not ok:
            print(f"    TX: {tx!r}\n    RX: {rx!r}")

    ser.write(b"EXIT\r\n")
    rx = read_until(ser, lambda d: b"OK:BYE" in d)
    ok = b"OK:BYE" in rx
    failed += not ok
    print(f"[{'PASS' if ok else 'FAIL'}] команда EXIT: ответ {rx!r}")

    ser.close()
    print("ALL TESTS PASSED" if failed == 0 else f"FAILED: {failed}")
    return 0 if failed == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
