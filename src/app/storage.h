/*
 * Non-volatile storage for configuration and the event log
 *
 * Backed by the Zephyr settings subsystem on NVS (storage_partition).
 * Keys live under "fg/": "cfg", "boot", "evh" and "ev/<slot>".
 */
#ifndef FG_STORAGE_H_
#define FG_STORAGE_H_

#include <stddef.h>
#include <stdint.h>

#if defined(CONFIG_FG_PERSIST)

/* Mount storage, restore configuration and event log, count this boot */
int fg_storage_init(void);

/* Write or delete "fg/<key>" */
int fg_storage_save(const char *key, const void *data, size_t len);
int fg_storage_delete(const char *key);

/* Number of boots recorded in storage, 1 on the first boot */
uint16_t fg_storage_boot_count(void);

#else

static inline int fg_storage_init(void)
{
	return 0;
}

static inline int fg_storage_save(const char *key, const void *data, size_t len)
{
	return 0;
}

static inline int fg_storage_delete(const char *key)
{
	return 0;
}

static inline uint16_t fg_storage_boot_count(void)
{
	return 0;
}

#endif /* CONFIG_FG_PERSIST */

#endif /* FG_STORAGE_H_ */
