/*
 * ЛР3. UART-сервис генерации ключевого материала (Zephyr 3.7.0, OpenTitan Earl Grey).
 *
 * Команды (по одной в строке, конец строки - \r или \n):
 *   STATUS - состояние сервиса и результат psa_crypto_init()
 *   GENKEY - 32 случайных байта (256 бит) из psa_generate_random() в HEX
 *   EXIT   - завершение сервиса
 * Без рабочего источника энтропии GENKEY отвечает ошибкой: запасного
 * некриптографического генератора в сервисе нет.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <psa/crypto.h>
#include <mbedtls/platform_util.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define LINE_MAX_LEN 32
#define KEY_BYTES 32

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static psa_status_t psa_init_status;

static void reply(const char *s)
{
    while (*s != '\0') {
        uart_poll_out(uart, *s++);
    }
    uart_poll_out(uart, '\r');
    uart_poll_out(uart, '\n');
}

/* Читает строку до \r или \n. Возвращает длину или -1, если строка не влезла */
static int read_line(char *buf, size_t size)
{
    size_t len = 0;
    bool too_long = false;
    unsigned char c;

    while (true) {
        if (uart_poll_in(uart, &c) != 0) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            break;
        }
        if (len < size - 1) {
            buf[len++] = (char)c;
        } else {
            too_long = true;
        }
    }
    buf[len] = '\0';
    return too_long ? -1 : (int)len;
}

static void cmd_genkey(void)
{
    uint8_t key[KEY_BYTES];
    char hex[2 * KEY_BYTES + 1];
    char out[sizeof("KEY:") + sizeof(hex)];
    psa_status_t st;

    if (psa_init_status != PSA_SUCCESS) {
        snprintf(out, sizeof(out), "ERR:NO_ENTROPY psa=%d", (int)psa_init_status);
        reply(out);
        return;
    }

    st = psa_generate_random(key, sizeof(key));
    if (st != PSA_SUCCESS) {
        snprintf(out, sizeof(out), "ERR:KEYGEN psa=%d", (int)st);
        reply(out);
        return;
    }

    bin2hex(key, sizeof(key), hex, sizeof(hex));
    snprintf(out, sizeof(out), "KEY:%s", hex);
    reply(out);

    /* Затираем ключ так, чтобы компилятор не выбросил обнуление */
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_platform_zeroize(hex, sizeof(hex));
    mbedtls_platform_zeroize(out, sizeof(out));
}

int main(void)
{
    char line[LINE_MAX_LEN];
    char out[48];
    int len;

    if (!device_is_ready(uart)) {
        printk("ERROR: UART not ready\n");
        return -ENODEV;
    }

    psa_init_status = psa_crypto_init();

    printk("Key generation service on %s\n", CONFIG_BOARD_TARGET);
    printk("psa_crypto_init: %d\n", (int)psa_init_status);
    printk("READY\n");

    while (true) {
        len = read_line(line, sizeof(line));
        if (len == 0) {
            continue; /* пустая строка, например \n после \r */
        }

        if (len < 0) {
            reply("ERR:TOO_LONG");
        } else if (strcmp(line, "STATUS") == 0) {
            snprintf(out, sizeof(out), "OK:READY psa=%d", (int)psa_init_status);
            reply(out);
        } else if (strcmp(line, "GENKEY") == 0) {
            cmd_genkey();
        } else if (strcmp(line, "EXIT") == 0) {
            reply("OK:BYE");
            break;
        } else {
            reply("ERR:UNKNOWN_CMD");
        }
    }

    printk("Key generation service stopped\n");
    return 0;
}
