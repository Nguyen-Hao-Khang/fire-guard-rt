/*
 * Sensor module helpers shared with shell/tests
 */
#ifndef FG_SENSORS_H_
#define FG_SENSORS_H_

#include <stdbool.h>
#include <stdint.h>

bool fg_rfid_is_commander(const uint8_t *uid, uint8_t len);
void fg_rfid_inject(const uint8_t *uid, uint8_t len);

/* Test hook: replace the MQ-2 reading with mv, or restore it with -1 */
void fg_adc_override_gas(int32_t mv);

/* Extra MQTT publications per second for load tests (CoreIoT builds) */
void fg_comm_set_load(uint32_t hz);

#endif /* FG_SENSORS_H_ */
