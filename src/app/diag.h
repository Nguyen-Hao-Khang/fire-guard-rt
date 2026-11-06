/*
 * Runtime diagnostics (thread CPU usage and data flow)
 */
#ifndef FG_DIAG_H_
#define FG_DIAG_H_

#include <stdint.h>

/* Print thread usage since the previous report and latest sensor data */
void fg_diag_report(void);

/* Called from the fault manager loop; reports every CONFIG_FG_DIAG_PERIOD_MS */
void fg_diag_tick(uint32_t now_ms);

#endif /* FG_DIAG_H_ */
