#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "perf.h"

#if !defined(__arm__)
#error This code can only be compiled for ARM architecture.
#endif

#define NR_EVENTS 4

int main(int argc, char *argv[]) {
  int cpu;

  if (argc < 2)
    cpu = 0;
  else
    cpu = atoi(argv[1]);

  int events[NR_EVENTS] = { 0x08, 0x14, 0x04, 0x03 };

  char* event_names[NR_EVENTS + 1] = {
    "Cycle count",
    "INST_RETIRED",
    "L1I_CACHE",
    "L1D_CACHE",
    "L1D_CACHE_REFILL"
  };

  struct perf_struct *pf = perf_initialize(cpu, NR_EVENTS, events);

  int i;
  for (i = 0; i < NR_EVENTS + 1; i++) perf_resetcounter(pf, i);

  for (i = 0; i < NR_EVENTS + 1; i++) perf_enablecounter(pf, i);

  // some computation
  char *x = malloc(128 * 1024 * 1024);
  for (i = 0; i < 128 * 1024 * 1024; i++) x[i] = (char)i;

  for (i = 0; i < NR_EVENTS + 1; i++) perf_disablecounter(pf, i);

  struct perf_read_format data;

  printf("                             Count    Time Enabled  Time Running\n");
  for (i = 0; i < NR_EVENTS + 1; i++) {
    perf_readcounter(pf, i, &data),
    printf("%24s%12" PRIu64 " %12" PRIu64 " %12" PRIu64 "\n",
            event_names[i], data.value, data.time_enabled, data.time_running);
  }

  perf_close(pf);
  free(x);

  return 0;
}
