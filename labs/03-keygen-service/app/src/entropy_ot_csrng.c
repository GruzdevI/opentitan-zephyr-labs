/*
 * Драйвер энтропии Zephyr для CSRNG OpenTitan Earl Grey.
 *
 * В Zephyr 3.7 такого драйвера нет, поэтому он лежит в приложении.
 * Порядок включения повторяет криптобиблиотеку OpenTitan
 * (sw/device/lib/crypto/drivers/entropy.c): entropy_src в FIPS-режиме
 * с аппаратным кондиционированием, данные идут в CSRNG; затем CSRNG и
 * команда Instantiate с затравкой из entropy_src. get_entropy() выдаёт
 * результат команды Generate блоками по 128 бит.
 * Регистры - csrng.hjson и entropy_src.hjson (OpenTitan earlgrey_1.0.0).
 */
#define DT_DRV_COMPAT lowrisc_opentitan_csrng

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

/* CSRNG */
#define CSRNG_CTRL            0x14
#define CSRNG_CMD_REQ         0x18
#define CSRNG_SW_CMD_STS      0x2c
#define CSRNG_GENBITS_VLD     0x30
#define CSRNG_GENBITS         0x34
#define CSRNG_RECOV_ALERT_STS 0x50
#define CSRNG_ERR_CODE        0x54
#define CSRNG_MAIN_SM_STATE   0x5c
#define CSRNG_CTRL_RESVAL     0x9999

#define STS_CMD_RDY      BIT(1)
#define STS_CMD_ACK      BIT(2)
#define STS_CMD_STS(reg) (((reg) >> 3) & 0x7)
#define GENBITS_VLD      BIT(0)

/* entropy_src */
#define ES_MODULE_ENABLE   0x20
#define ES_CONF            0x24
#define ES_ENTROPY_CONTROL 0x28
#define ES_DEBUG_STATUS    0xd0
#define ES_RECOV_ALERT_STS 0xd4
#define ES_ERR_CODE        0xd8
#define ES_MAIN_SM_STATE   0xe0

/* Многобитовые булевы значения OpenTitan */
#define MUBI4_TRUE  0x6
#define MUBI4_FALSE 0x9

/* CONF: FIPS_ENABLE[3:0], FIPS_FLAG[7:4], RNG_FIPS[11:8], RNG_BIT_ENABLE[15:12],
 * THRESHOLD_SCOPE[21:18], ENTROPY_DATA_REG_ENABLE[25:22] */
#define ES_CONF_FIPS (MUBI4_TRUE | (MUBI4_TRUE << 4) | (MUBI4_TRUE << 8) | \
                      (MUBI4_FALSE << 12) | (MUBI4_FALSE << 18) | (MUBI4_FALSE << 22))
/* ENTROPY_CONTROL: ES_ROUTE = false (в CSRNG), ES_TYPE = false (с кондиционированием) */
#define ES_CONTROL_HW (MUBI4_FALSE | (MUBI4_FALSE << 4))
/* CSRNG CTRL: ENABLE, SW_APP_ENABLE, READ_INT_STATE = true; FIPS_FORCE_ENABLE = false */
#define CSRNG_CTRL_ON (MUBI4_TRUE | (MUBI4_TRUE << 4) | (MUBI4_TRUE << 8) | (MUBI4_FALSE << 12))

/* Заголовок команды: acmd[3:0], clen[7:4], flag0[11:8], glen[30:12].
 * flag0 = false - затравка берётся из entropy_src */
#define CMD_INSTANTIATE 1
#define CMD_GENERATE    3
#define CMD_HEADER(acmd, glen) ((acmd) | (MUBI4_FALSE << 8) | ((uint32_t)(glen) << 12))

#define MAX_GEN_BLOCKS 0x800  /* предел NIST SP 800-90A на одну команду */
#define POLL_LIMIT     200000

struct ot_csrng_config {
    mm_reg_t csrng;
    mm_reg_t es;
};

/* Состояние регистров после запуска - для журнала */
static struct {
    int init_ret;
    uint32_t es_sm, es_dbg, es_recov, es_err;
    uint32_t cs_sts, cs_sm, cs_recov, cs_err;
} diag;

static int wait_bits(mm_reg_t reg, uint32_t mask)
{
    for (int i = 0; i < POLL_LIMIT; i++) {
        if ((sys_read32(reg) & mask) == mask) {
            return 0;
        }
    }
    return -ETIMEDOUT;
}

static int send_cmd(const struct ot_csrng_config *cfg, uint32_t header)
{
    if (wait_bits(cfg->csrng + CSRNG_SW_CMD_STS, STS_CMD_RDY) != 0) {
        return -ETIMEDOUT;
    }
    sys_write32(header, cfg->csrng + CSRNG_CMD_REQ);
    return 0;
}

static int wait_ack(const struct ot_csrng_config *cfg)
{
    if (wait_bits(cfg->csrng + CSRNG_SW_CMD_STS, STS_CMD_ACK) != 0) {
        return -ETIMEDOUT;
    }
    return STS_CMD_STS(sys_read32(cfg->csrng + CSRNG_SW_CMD_STS)) == 0 ? 0 : -EIO;
}

/* Одна команда Generate на n байт (не больше MAX_GEN_BLOCKS блоков) */
static int generate(const struct ot_csrng_config *cfg, uint8_t *buf, size_t n)
{
    size_t blocks = DIV_ROUND_UP(n, 16);
    uint32_t words[4];
    int ret;

    ret = send_cmd(cfg, CMD_HEADER(CMD_GENERATE, blocks));
    if (ret != 0) {
        return ret;
    }

    for (size_t b = 0; b < blocks; b++) {
        size_t chunk = MIN(n, sizeof(words));

        if (wait_bits(cfg->csrng + CSRNG_GENBITS_VLD, GENBITS_VLD) != 0) {
            return -ETIMEDOUT;
        }
        for (int i = 0; i < 4; i++) {
            words[i] = sys_read32(cfg->csrng + CSRNG_GENBITS);
        }
        memcpy(buf, words, chunk);
        buf += chunk;
        n -= chunk;
    }
    memset(words, 0, sizeof(words));

    return wait_ack(cfg);
}

static int ot_csrng_get_entropy(const struct device *dev, uint8_t *buf, uint16_t len)
{
    const struct ot_csrng_config *cfg = dev->config;

    while (len > 0) {
        size_t n = MIN(len, MAX_GEN_BLOCKS * 16);
        int ret = generate(cfg, buf, n);

        if (ret != 0) {
            return ret;
        }
        buf += n;
        len -= n;
    }
    return 0;
}

static void save_diag(const struct ot_csrng_config *cfg, int ret)
{
    diag.init_ret = ret;
    diag.es_sm = sys_read32(cfg->es + ES_MAIN_SM_STATE);
    diag.es_dbg = sys_read32(cfg->es + ES_DEBUG_STATUS);
    diag.es_recov = sys_read32(cfg->es + ES_RECOV_ALERT_STS);
    diag.es_err = sys_read32(cfg->es + ES_ERR_CODE);
    diag.cs_sts = sys_read32(cfg->csrng + CSRNG_SW_CMD_STS);
    diag.cs_sm = sys_read32(cfg->csrng + CSRNG_MAIN_SM_STATE);
    diag.cs_recov = sys_read32(cfg->csrng + CSRNG_RECOV_ALERT_STS);
    diag.cs_err = sys_read32(cfg->csrng + CSRNG_ERR_CODE);
}

static int ot_csrng_init(const struct device *dev)
{
    const struct ot_csrng_config *cfg = dev->config;
    int ret;

    /* Остановить оба блока: настраивать entropy_src можно только выключенным */
    sys_write32(CSRNG_CTRL_RESVAL, cfg->csrng + CSRNG_CTRL);
    sys_write32(MUBI4_FALSE, cfg->es + ES_MODULE_ENABLE);

    /* entropy_src: FIPS-режим, данные в CSRNG; окна и пороги тестов - по умолчанию */
    sys_write32(ES_CONTROL_HW, cfg->es + ES_ENTROPY_CONTROL);
    sys_write32(ES_CONF_FIPS, cfg->es + ES_CONF);
    sys_write32(0, cfg->es + ES_RECOV_ALERT_STS);
    sys_write32(MUBI4_TRUE, cfg->es + ES_MODULE_ENABLE);

    sys_write32(0, cfg->csrng + CSRNG_RECOV_ALERT_STS);
    sys_write32(CSRNG_CTRL_ON, cfg->csrng + CSRNG_CTRL);

    ret = send_cmd(cfg, CMD_HEADER(CMD_INSTANTIATE, 0));
    if (ret == 0) {
        ret = wait_ack(cfg);
    }
    save_diag(cfg, ret);
    return ret;
}

static const struct entropy_driver_api ot_csrng_api = {
    .get_entropy = ot_csrng_get_entropy,
};

static const struct ot_csrng_config ot_csrng_cfg = {
    .csrng = DT_INST_REG_ADDR_BY_NAME(0, csrng),
    .es = DT_INST_REG_ADDR_BY_NAME(0, entropy_src),
};

DEVICE_DT_INST_DEFINE(0, ot_csrng_init, NULL, NULL, &ot_csrng_cfg,
                      PRE_KERNEL_1, CONFIG_ENTROPY_INIT_PRIORITY, &ot_csrng_api);

/* Перед main() сообщаем в журнал, запустился ли драйвер, и состояние блоков */
static int ot_csrng_report(void)
{
    const struct device *dev = DEVICE_DT_INST_GET(0);

    printk("Entropy: %s %s (init %d)\n", dev->name,
           device_is_ready(dev) ? "ready" : "NOT READY", diag.init_ret);
    printk("  entropy_src: sm=0x%03x dbg=0x%08x recov=0x%x err=0x%x\n",
           diag.es_sm, diag.es_dbg, diag.es_recov, diag.es_err);
    printk("  csrng: sts=0x%x sm=0x%03x recov=0x%x err=0x%x\n",
           diag.cs_sts, diag.cs_sm, diag.cs_recov, diag.cs_err);
    return 0;
}
SYS_INIT(ot_csrng_report, APPLICATION, 0);
