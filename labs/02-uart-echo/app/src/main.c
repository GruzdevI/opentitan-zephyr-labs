/*
 * ЛР2. UART-сервис эхо-повтора для OpenTitan Earl Grey (Zephyr 3.7.0).
 *
 * Каждый принятый байт сразу отправляется обратно. Если строка целиком
 * равна EXIT (конец строки - \r или \n), сервис отвечает OK:BYE
 * и завершает работу.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Хватает для команды EXIT; более длинные строки только возвращаются */
#define CMD_MAX 16

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_poll_out(uart, *s++);
    }
}

int main(void)
{
    char cmd[CMD_MAX];
    size_t len = 0;
    bool too_long = false;
    unsigned int echoed = 0;
    unsigned char c;

    if (!device_is_ready(uart)) {
        printk("ERROR: UART not ready\n");
        return -ENODEV;
    }

    printk("UART echo service on %s\n", CONFIG_BOARD_TARGET);
    printk("READY\n");

    while (true) {
        /* Опрос без прерываний: ненулевой код - байта пока нет */
        if (uart_poll_in(uart, &c) != 0) {
            continue;
        }

        uart_poll_out(uart, c);
        echoed++;

        if (c == '\r' || c == '\n') {
            if (!too_long && len == 4 && memcmp(cmd, "EXIT", 4) == 0) {
                break;
            }
            len = 0;
            too_long = false;
        } else if (len < CMD_MAX) {
            cmd[len++] = (char)c;
        } else {
            too_long = true;
        }
    }

    uart_puts("\r\nOK:BYE\r\n");
    printk("Echoed %u bytes, service stopped\n", echoed);
    return 0;
}
