/*
 * Wokwi custom chip: MFRC522 RFID reader (behavioural model)
 *
 * Emulates the SPI register interface used by src/drivers/rc522.c:
 *   VersionReg, CommandReg (Idle / Transceive / SoftReset), ComIrqReg,
 *   FIFODataReg, FIFOLevelReg, BitFramingReg (StartSend), ErrorReg.
 * A card in the field answers REQA/WUPA with ATQA and the cascade-level-1
 * anticollision command with UID + BCC. Controls:
 *   card          0 = no card, 1 = commander DE:AD:BE:EF, 2 = unknown 12:34:56:78
 *   disconnected  1 = reader stops answering (VersionReg reads 0x00)
 */
#include "wokwi-api.h"

#define REG_COMMAND     0x01
#define REG_COM_IRQ     0x04
#define REG_ERROR       0x06
#define REG_FIFO_DATA   0x09
#define REG_FIFO_LEVEL  0x0A
#define REG_CONTROL     0x0C
#define REG_BIT_FRAMING 0x0D
#define REG_VERSION     0x37

#define CMD_IDLE        0x00
#define CMD_TRANSCEIVE  0x0C
#define CMD_SOFT_RESET  0x0F

#define FIFO_SIZE 64

typedef struct {
  pin_t cs;
  pin_t rst;
  spi_dev_t spi;
  uint32_t attr_card;
  uint32_t attr_disconnected;
  uint8_t buf[1];
  uint8_t regs[64];
  uint8_t fifo[FIFO_SIZE];
  uint8_t fifo_len;
  uint8_t fifo_rd;
  uint8_t com_irq;
  uint8_t command;
  bool first;
  bool read_mode;
  uint8_t addr;
} chip_state_t;

static chip_state_t chip;

static const uint8_t UID_COMMANDER[4] = {0xDE, 0xAD, 0xBE, 0xEF};
static const uint8_t UID_UNKNOWN[4] = {0x12, 0x34, 0x56, 0x78};

static void chip_reset(chip_state_t *c) {
  for (int i = 0; i < 64; i++) {
    c->regs[i] = 0;
  }
  c->regs[REG_VERSION] = 0x92;
  c->fifo_len = 0;
  c->fifo_rd = 0;
  c->com_irq = 0x14;
  c->command = CMD_IDLE;
}

static bool disconnected(chip_state_t *c) {
  return attr_read(c->attr_disconnected) != 0 || pin_read(c->rst) == LOW;
}

static void fifo_push(chip_state_t *c, uint8_t v) {
  if (c->fifo_len < FIFO_SIZE) {
    c->fifo[c->fifo_len++] = v;
  }
}

static void fifo_set(chip_state_t *c, const uint8_t *data, uint8_t n) {
  c->fifo_len = 0;
  c->fifo_rd = 0;
  for (uint8_t i = 0; i < n; i++) {
    fifo_push(c, data[i]);
  }
}

static void transceive(chip_state_t *c) {
  uint8_t req[FIFO_SIZE];
  uint8_t n = c->fifo_len - c->fifo_rd;
  uint32_t card = attr_read(c->attr_card);
  const uint8_t *uid = card == 1 ? UID_COMMANDER : UID_UNKNOWN;

  for (uint8_t i = 0; i < n; i++) {
    req[i] = c->fifo[c->fifo_rd + i];
  }
  c->fifo_len = 0;
  c->fifo_rd = 0;
  c->regs[REG_BIT_FRAMING] &= 0x7F;

  if (card == 0 || n == 0) {
    c->com_irq |= 0x01; /* TimerIRq: nobody answered */
    return;
  }

  if (n == 1 && (req[0] == 0x26 || req[0] == 0x52)) {
    const uint8_t atqa[2] = {0x04, 0x00};
    fifo_set(c, atqa, 2);
  } else if (n == 2 && req[0] == 0x93 && req[1] == 0x20) {
    uint8_t resp[5] = {uid[0], uid[1], uid[2], uid[3],
                       (uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3])};
    fifo_set(c, resp, 5);
  } else {
    c->com_irq |= 0x01; /* unsupported command: behave like a timeout */
    return;
  }
  c->com_irq |= 0x30; /* RxIRq | IdleIRq */
}

static uint8_t reg_read(chip_state_t *c, uint8_t addr) {
  if (disconnected(c)) {
    return 0x00;
  }
  switch (addr) {
  case REG_COMMAND:
    return c->command;
  case REG_COM_IRQ:
    return c->com_irq;
  case REG_ERROR:
    return 0x00;
  case REG_FIFO_DATA:
    return (c->fifo_rd < c->fifo_len) ? c->fifo[c->fifo_rd++] : 0x00;
  case REG_FIFO_LEVEL:
    return c->fifo_len - c->fifo_rd;
  case REG_CONTROL:
    return 0x10;
  default:
    return c->regs[addr & 0x3F];
  }
}

static void reg_write(chip_state_t *c, uint8_t addr, uint8_t v) {
  if (disconnected(c)) {
    return;
  }
  switch (addr) {
  case REG_COMMAND:
    c->command = v & 0x0F;
    if (c->command == CMD_SOFT_RESET) {
      chip_reset(c);
    }
    break;
  case REG_COM_IRQ:
    if (v & 0x80) {
      c->com_irq |= (v & 0x7F);
    } else {
      c->com_irq &= ~(v & 0x7F);
    }
    break;
  case REG_FIFO_DATA:
    fifo_push(c, v);
    break;
  case REG_FIFO_LEVEL:
    if (v & 0x80) {
      c->fifo_len = 0;
      c->fifo_rd = 0;
    }
    break;
  case REG_BIT_FRAMING:
    c->regs[REG_BIT_FRAMING] = v;
    if ((v & 0x80) && c->command == CMD_TRANSCEIVE) {
      transceive(c);
    }
    break;
  default:
    c->regs[addr & 0x3F] = v;
    break;
  }
}

static void spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *c = (chip_state_t *)user_data;

  if (count == 0) {
    return;
  }
  uint8_t b = buffer[0];

  if (c->first) {
    c->first = false;
    c->addr = (b >> 1) & 0x3F;
    c->read_mode = (b & 0x80) != 0;
    c->buf[0] = c->read_mode ? reg_read(c, c->addr) : 0x00;
  } else if (c->read_mode) {
    /* each byte clocked in is the next address to read (0 terminates) */
    c->buf[0] = b ? reg_read(c, (b >> 1) & 0x3F) : 0x00;
  } else {
    reg_write(c, c->addr, b);
    c->buf[0] = 0x00;
  }

  if (pin_read(c->cs) == LOW) {
    spi_start(c->spi, c->buf, 1);
  }
}

static void cs_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *c = (chip_state_t *)user_data;

  if (value == LOW) {
    c->first = true;
    c->buf[0] = 0x00;
    spi_start(c->spi, c->buf, 1);
  } else {
    spi_stop(c->spi);
  }
}

void chip_init(void) {
  chip_state_t *c = &chip;

  c->cs = pin_init("SDA", INPUT_PULLUP);
  c->rst = pin_init("RST", INPUT_PULLUP);
  c->attr_card = attr_init("card", 0);
  c->attr_disconnected = attr_init("disconnected", 0);
  chip_reset(c);

  const spi_config_t spi_cfg = {
    .sck = pin_init("SCK", INPUT),
    .mosi = pin_init("MOSI", INPUT),
    .miso = pin_init("MISO", INPUT),
    .done = spi_done,
    .user_data = c,
    .mode = 0,
  };
  c->spi = spi_init(&spi_cfg);

  const pin_watch_config_t cs_cfg = {
    .edge = BOTH,
    .pin_change = cs_change,
    .user_data = c,
  };
  pin_watch(c->cs, &cs_cfg);
}
