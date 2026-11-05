/*
 * Minimal MFRC522 driver over Zephyr SPI
 *
 * SPI address byte: read = 1aaaaaa0, write = 0aaaaaa0 (MFRC522 datasheet 8.1.2.3)
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "drivers/rc522.h"

LOG_MODULE_REGISTER(rc522, LOG_LEVEL_INF);

#define RC522_NODE DT_NODELABEL(rfid)

/* Registers */
#define REG_COMMAND     0x01
#define REG_COM_IRQ     0x04
#define REG_ERROR       0x06
#define REG_FIFO_DATA   0x09
#define REG_FIFO_LEVEL  0x0A
#define REG_CONTROL     0x0C
#define REG_BIT_FRAMING 0x0D
#define REG_COLL        0x0E
#define REG_MODE        0x11
#define REG_TX_CONTROL  0x14
#define REG_TX_ASK      0x15
#define REG_T_MODE      0x2A
#define REG_T_PRESCALER 0x2B
#define REG_T_RELOAD_H  0x2C
#define REG_T_RELOAD_L  0x2D
#define REG_VERSION     0x37

/* Commands */
#define CMD_IDLE        0x00
#define CMD_TRANSCEIVE  0x0C
#define CMD_SOFT_RESET  0x0F

/* PICC */
#define PICC_REQA       0x26
#define PICC_SEL_CL1    0x93

#define IRQ_RX          0x20
#define IRQ_IDLE        0x10
#define IRQ_ERR         0x02
#define IRQ_TIMER       0x01

static const struct spi_dt_spec bus =
	SPI_DT_SPEC_GET(RC522_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER);
static const struct gpio_dt_spec rst = GPIO_DT_SPEC_GET_OR(RC522_NODE, reset_gpios, {0});

static int reg_write(uint8_t reg, uint8_t val)
{
	uint8_t tx[2] = {(reg << 1) & 0x7E, val};
	const struct spi_buf b = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf_set s = {.buffers = &b, .count = 1};

	return spi_write_dt(&bus, &s);
}

static int reg_read(uint8_t reg, uint8_t *val)
{
	uint8_t tx[2] = {((reg << 1) & 0x7E) | 0x80, 0x00};
	uint8_t rx[2];
	const struct spi_buf tb = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf rb = {.buf = rx, .len = sizeof(rx)};
	const struct spi_buf_set ts = {.buffers = &tb, .count = 1};
	const struct spi_buf_set rs = {.buffers = &rb, .count = 1};
	int err = spi_transceive_dt(&bus, &ts, &rs);

	if (err == 0) {
		*val = rx[1];
	}
	return err;
}

static int reg_set_bits(uint8_t reg, uint8_t mask, bool set)
{
	uint8_t v;
	int err = reg_read(reg, &v);

	if (err) {
		return err;
	}
	return reg_write(reg, set ? (v | mask) : (v & ~mask));
}

int rc522_version(void)
{
	uint8_t v;
	int err = reg_read(REG_VERSION, &v);

	return err ? err : v;
}

int rc522_init(void)
{
	int v;

	if (!spi_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	if (rst.port && gpio_is_ready_dt(&rst)) {
		gpio_pin_configure_dt(&rst, GPIO_OUTPUT_ACTIVE); /* hold in reset */
		k_msleep(2);
		gpio_pin_set_dt(&rst, 0);
		k_msleep(50);
	}

	reg_write(REG_COMMAND, CMD_SOFT_RESET);
	k_msleep(50);

	/* Timer: ~25 ms receive timeout (TPrescaler 0xA9 -> 40 kHz, reload 0x3E8) */
	reg_write(REG_T_MODE, 0x80);
	reg_write(REG_T_PRESCALER, 0xA9);
	reg_write(REG_T_RELOAD_H, 0x03);
	reg_write(REG_T_RELOAD_L, 0xE8);
	reg_write(REG_TX_ASK, 0x40);  /* 100 % ASK */
	reg_write(REG_MODE, 0x3D);    /* CRC preset 0x6363 */
	reg_set_bits(REG_TX_CONTROL, 0x03, true); /* antenna on */

	v = rc522_version();
	if (v < 0 || v == 0x00 || v == 0xFF) {
		LOG_ERR("MFRC522 not found (VersionReg=0x%02x)", v);
		return -ENODEV;
	}
	LOG_INF("MFRC522 ready, VersionReg=0x%02x", v);
	return 0;
}

/* Send tx_len bytes, receive into rx. Returns received byte count or <0 */
static int transceive(const uint8_t *tx, uint8_t tx_len, uint8_t tx_last_bits, uint8_t *rx,
		      uint8_t rx_max)
{
	uint8_t irq = 0, err_reg, level;

	reg_write(REG_COMMAND, CMD_IDLE);
	reg_write(REG_COM_IRQ, 0x7F);           /* clear all IRQ flags */
	reg_write(REG_FIFO_LEVEL, 0x80);        /* flush FIFO */
	for (uint8_t i = 0; i < tx_len; i++) {
		reg_write(REG_FIFO_DATA, tx[i]);
	}
	reg_write(REG_BIT_FRAMING, tx_last_bits & 0x07);
	reg_write(REG_COMMAND, CMD_TRANSCEIVE);
	reg_set_bits(REG_BIT_FRAMING, 0x80, true); /* StartSend */

	int64_t deadline = k_uptime_get() + 36;

	do {
		if (reg_read(REG_COM_IRQ, &irq)) {
			return -EIO;
		}
		if (irq & (IRQ_RX | IRQ_IDLE)) {
			break;
		}
		if (irq & IRQ_TIMER) {
			return 0; /* no card answered */
		}
		k_usleep(500);
	} while (k_uptime_get() < deadline);

	reg_set_bits(REG_BIT_FRAMING, 0x80, false);

	if (!(irq & (IRQ_RX | IRQ_IDLE))) {
		return 0;
	}
	if (reg_read(REG_ERROR, &err_reg) || (err_reg & 0x13)) { /* BufferOvfl, Parity, Protocol */
		return -EPROTO;
	}
	if (reg_read(REG_FIFO_LEVEL, &level)) {
		return -EIO;
	}
	level = MIN(level, rx_max);
	for (uint8_t i = 0; i < level; i++) {
		reg_read(REG_FIFO_DATA, &rx[i]);
	}
	return level;
}

int rc522_read_uid(uint8_t uid[RC522_UID_MAX])
{
	uint8_t atqa[2];
	uint8_t req = PICC_REQA;
	uint8_t anticoll[2] = {PICC_SEL_CL1, 0x20};
	uint8_t resp[5];
	int n;

	reg_write(REG_COLL, 0x80); /* ValuesAfterColl */
	n = transceive(&req, 1, 7, atqa, sizeof(atqa)); /* REQA is a 7-bit frame */
	if (n <= 0) {
		return n;
	}
	if (n != 2) {
		return -EPROTO;
	}

	n = transceive(anticoll, 2, 0, resp, sizeof(resp));
	if (n <= 0) {
		return n < 0 ? n : -EPROTO;
	}
	if (n != 5 || (resp[0] ^ resp[1] ^ resp[2] ^ resp[3]) != resp[4]) {
		return -EPROTO; /* BCC mismatch */
	}
	for (int i = 0; i < 4; i++) {
		uid[i] = resp[i];
	}
	return 4;
}
