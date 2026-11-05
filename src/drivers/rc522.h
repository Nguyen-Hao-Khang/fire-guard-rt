/*
 * Minimal MFRC522 driver (ISO 14443A, UID read only)
 */
#ifndef FG_RC522_H_
#define FG_RC522_H_

#include <stdint.h>

#define RC522_UID_MAX 10

/* Reset + configure the reader. Returns 0 or -ENODEV if VersionReg is invalid */
int rc522_init(void);

/* Returns VersionReg (0x91/0x92 for genuine chips, 0x88 for clones) or <0 */
int rc522_version(void);

/*
 * Poll for a card: REQA + anticollision cascade level 1.
 * Returns UID length (4) on success, 0 if no card, <0 on bus/protocol error.
 */
int rc522_read_uid(uint8_t uid[RC522_UID_MAX]);

#endif /* FG_RC522_H_ */
