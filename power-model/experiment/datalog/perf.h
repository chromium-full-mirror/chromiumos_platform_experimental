/*
 * Library for reading performance counters using the kernel perf_event_open
 * system call interface.
 *
 */

#ifndef PERF_H_
#define PERF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* perf data structure */
struct perf_struct;

/* perf read data format */
struct perf_read_format {
  uint64_t value;        /* The value of the event */
  uint64_t time_enabled; /* if PERF_FORMAT_TOTAL_TIME_ENABLED */
  uint64_t time_running; /* if PERF_FORMAT_TOTAL_TIME_RUNNING */
};

/* perf_initialize
 *
 * Initialize the performance counters to count a set of events.
 * The cycle counter is always enabled.
 *
 * Params:
 *  cpu: the cpu id to monitor
 *  n_events: number of events to monitor
 *  events: an array of event IDs
 *
 * Return:
 *  a pointer to a perf_struct data structure
 */
struct perf_struct *perf_initialize(int cpu, int n_events, int *events);

/* perf_close
 *
 * Close and clean up the performance counters.
 *
 * Params:
 *  pf: a pointer to perf_struct data structure
 */
void perf_close(struct perf_struct *pf);

/* perf_enablecounter
 *
 * Enable performance counters.
 *
 * Params:
 *  pf: a pointer to perf_struct data structure
 *  counter: the index of the counter to enable
 *
 * Return:
 *  0 on success
 */
int perf_enablecounter(struct perf_struct *pf, int counter);

/* perf_disablecounter
 *
 * Disable performance counters.
 *
 * Params:
 *  pf: a pointer to perf_struct data structure
 *  counter: the index of the counter to disable
 *
 * Return:
 *  0 on success
 */
int perf_disablecounter(struct perf_struct *pf, int counter);

/* perf_resetcounter
 *
 * Reset performance counters.
 *
 * Params:
 *  pf: a pointer to perf_struct data structure
 *  counter: the index of the counter to reset
 *
 * Return:
 *  0 on success
 */
int perf_resetcounter(struct perf_struct *pf, int counter);

/* perf_readcounter
 *
 * Read performance counters.
 *
 * Params:
 *  pf: a pointer to perf_struct data structure
 *  counter: the index of the counter to read
 */
void perf_readcounter(struct perf_struct *pf, int counter,
                      struct perf_read_format *data);

/* perf_readcountervalue
 *
 * Read performance counters (only value field).
 *
 * Params:
 *  pf: a pointer to perf_struct data structure
 *  counter: the index of the counter to read
 *
 * Return:
 *  counter value
 */
uint64_t perf_readcountervalue(struct perf_struct *pf, int counter);

#ifdef __cplusplus
}
#endif

#endif /* PERF_H_ */
