/*
 * Library for reading performance counters using the kernel perf_event_open
 * system call interface.
 *
 */

#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>

#include "perf.h"

#define PERF_MAX_COUNTERS 7

struct perf_struct {
  int cpu;
  int n_counters;
  int fds[PERF_MAX_COUNTERS];
  struct perf_event_attr *attrs;
};

/* perf_event_open syscall wrapper */
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  int ret;

  ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
  return ret;
}

/* malloc wrapper that checks for failure */
static void *check_malloc(size_t size)
{
  void *p = malloc(size);

  if (p == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  return p;
}

/* Initialize the performance counters to count a set of events.
 *
 * The cycle counter is always enabled.
 */
struct perf_struct *perf_initialize(int cpu, int n_events, int *events) {
  int n_counters = n_events + 1;

  if (n_counters > PERF_MAX_COUNTERS) {
    perror("Exceeded the maximum number of counters!");
    exit(EXIT_FAILURE);
  }

  int i;

  struct perf_struct *pf = check_malloc(sizeof(struct perf_struct));

  pf->cpu = cpu;
  pf->n_counters = n_counters;

  for (i = 0; i < PERF_MAX_COUNTERS; i++)
    pf->fds[i] = -1;

  struct perf_event_attr *attrs =
      check_malloc(n_counters * sizeof(struct perf_event_attr));
  memset(attrs, 0, n_counters * sizeof(struct perf_event_attr));
  pf->attrs = attrs;

  /* Set up cycle counter */
  attrs[0].type = PERF_TYPE_HARDWARE;
  attrs[0].size = sizeof(struct perf_event_attr);
  attrs[0].config = PERF_COUNT_HW_CPU_CYCLES;
  attrs[0].read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
  attrs[0].inherit = 1;
  attrs[0].disabled = 1;

  /* Set up custom counters */
  for (i = 1; i < n_counters; i++) {
    attrs[i].type = PERF_TYPE_RAW;
    attrs[i].size = sizeof(struct perf_event_attr);
    attrs[i].config = (uint64_t) events[i-1];
    attrs[i].read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    attrs[i].inherit = 1;
    attrs[i].disabled = 1;
  }

  for (i = 0; i < n_counters; i++) {
    pf->fds[i] = perf_event_open(&attrs[i], -1, cpu, -1, 0);
    if (pf->fds[i] < 0) {
      fprintf(stderr, "At event %d/%d\n", i, n_counters);
      perror("perf_event_open");
      exit(EXIT_FAILURE);
    }
  }

  return pf;
}


/* Close performance counters. */
void perf_close(struct perf_struct *pf) {
  int i;

  int *fds = pf->fds;

  for (i = 0; i < pf->n_counters; i++) {
    assert(fds[i] >= 0);
    close(fds[i]);
  }

  free(pf->attrs);
  free(pf);
}

/* perf_event ioctl calls */
static int perf_ioctl(struct perf_struct *pf, int counter, int request)
{
  assert(counter >= 0 && counter < pf->n_counters);
  if (pf->fds[counter] == -1) {
    fprintf(stderr, "Counter %d has not been set up.\n", counter);
    return -1;
  }

  return ioctl(pf->fds[counter], request);
}

/* Enable performance counters. */
int perf_enablecounter(struct perf_struct *pf, int counter) {
  return perf_ioctl(pf, counter, PERF_EVENT_IOC_ENABLE);
}

/* Disable performance counters. */
int perf_disablecounter(struct perf_struct *pf, int counter) {
  return perf_ioctl(pf, counter, PERF_EVENT_IOC_DISABLE);
}

/* Reset performance counters. */
int perf_resetcounter(struct perf_struct *pf, int counter) {
  return perf_ioctl(pf, counter, PERF_EVENT_IOC_RESET);
}

/* Read performance counters. */
void perf_readcounter(struct perf_struct *pf, int counter,
                      struct perf_read_format *data) {
  assert(counter >= 0 && counter < pf->n_counters);

  assert(read(pf->fds[counter], data, sizeof(struct perf_read_format)) ==
         sizeof(struct perf_read_format));
}

/* Read performance counters (only value field). */
uint64_t perf_readcountervalue(struct perf_struct *pf, int counter) {
  struct perf_read_format data;
  perf_readcounter(pf, counter, &data);

  return data.value;
}
