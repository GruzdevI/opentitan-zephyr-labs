#!/usr/bin/env python3
"""ЛР3. Хост-клиент для проверки сервиса генерации ключей в модели OpenTitan.

Использование: python3 host_client.py /dev/pts/N
"""
import re
import sys
import time

import serial

TIMEOUT = 60.0   # секунд на ответ: модель под Rosetta работает медленно
KEY_COUNT = 3    # сколько ключей запросить
KEY_RE = re.compile(r"KEY:([0-9a-f]{64})")


def command(ser, cmd):
    """Отправляет команду, возвращает строку ответа без \\r\\n и время ответа."""
    start = time.monotonic()
    ser.write(cmd.encode() + b"\r\n")
    line = bytearray()
    while time.monotonic() < start + TIMEOUT and not line.endswith(b"\r\n"):
        line += ser.read(ser.in_waiting or 1)
    return line.decode(errors="replace").strip(), time.monotonic() - start


def check(name, ok, detail=""):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f": {detail}" if detail else ""))
    return ok


def main():
    if len(sys.argv) != 2:
        print(f"Использование: {sys.argv[0]} /dev/pts/N")
        return 1

    ser = serial.Serial(sys.argv[1], baudrate=115200, timeout=0.2)
    ser.reset_input_buffer()  # отбросить журнал загрузки
    results = []

    resp, _ = command(ser, "STATUS")
    results.append(check("STATUS", resp == "OK:READY psa=0", resp))

    keys = []
    for i in range(KEY_COUNT):
        resp, dt = command(ser, "GENKEY")
        m = KEY_RE.fullmatch(resp)
        if m:
            keys.append(bytes.fromhex(m.group(1)))
        results.append(check(f"GENKEY #{i + 1}", m is not None, f"{resp} ({dt:.1f} с)"))

    if len(keys) == KEY_COUNT:
        results.append(check("ключи различаются", len(set(keys)) == KEY_COUNT))
        ones = sum(bin(b).count("1") for k in keys for b in k)
        bits = 8 * len(keys[0]) * KEY_COUNT
        print(f"       {len(keys[0])} байта = {8 * len(keys[0])} бит на ключ; "
              f"единиц {ones} из {bits} ({100 * ones / bits:.1f} %)")

    resp, _ = command(ser, "HELLO")
    results.append(check("неизвестная команда", resp == "ERR:UNKNOWN_CMD", resp))

    resp, _ = command(ser, "A" * 40)
    results.append(check("слишком длинная команда", resp == "ERR:TOO_LONG", resp))

    resp, _ = command(ser, "EXIT")
    results.append(check("EXIT", resp == "OK:BYE", resp))

    ser.close()
    failed = results.count(False)
    print("ALL TESTS PASSED" if failed == 0 else f"FAILED: {failed}")
    return 0 if failed == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
