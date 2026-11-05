/*
 * Sensor module helpers shared with shell/tests
 */
#ifndef FG_SENSORS_H_
#define FG_SENSORS_H_

#include <stdbool.h>
#include <stdint.h>

bool fg_rfid_is_commander(const uint8_t *uid, uint8_t len);
void fg_rfid_inject(const uint8_t *uid, uint8_t len);

#endif /* FG_SENSORS_H_ */
