/*
 * sntp_time_sync.h
 */

#ifndef MAIN_SNTP_TIME_SYNC_H_
#define MAIN_SNTP_TIME_SYNC_H_

#include <stdbool.h>

/**
 * Starts the NTP synchronization task (safe to call once).
 */
void sntp_time_sync_task_start(void);

/**
 * Returns local time string if set.
 */
char* sntp_time_sync_get_time(void);

/**
 * Returns true if time looks valid.
 */
bool sntp_time_sync_is_time_set(void);

#endif /* MAIN_SNTP_TIME_SYNC_H_ */