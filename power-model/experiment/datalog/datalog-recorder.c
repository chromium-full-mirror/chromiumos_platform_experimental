#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sched.h>

#include "perf.h"

#if !defined(__arm__)
#error This code can only be compiled for ARM architecture.
#endif

#define LOG_HEADER "datalog     : "

/* Maximum number of custom-defined events supported by ARM PMUv3 */
#define MAX_CUSTOM_EVENTS 6

/* Events to record for a PMU */
typedef struct {
  int nr_events;
  int nr_unique_events;
  int events[MAX_CUSTOM_EVENTS];
  int unique_events[MAX_CUSTOM_EVENTS];
  int event_pointer[MAX_CUSTOM_EVENTS];
} pmu_events_t;

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

/* Print event codes */
static void print_events(int n_events, int events[]) {
  int i;
  for (i = 0; i < n_events; i++) {
    printf("0x%02x", events[i]);
    if (i != n_events - 1) printf(", ");
  }
  putchar('\n');
}

/* Read perf counters for a core and append to output file. */
static void appendCounts(FILE *out, struct perf_struct *pf,
                         int nr_events, int nr_unique, int *event_ptr) {
  struct perf_read_format data;
  uint64_t counts[MAX_CUSTOM_EVENTS + 1];

  int i;
  double dutyCycle;

  static int warned;

  for (i = 0; i < nr_unique + 1; i++) {
    perf_readcounter(pf, i, &data);
    if (data.time_running < data.time_enabled) {
      if (warned == 0) {
        fprintf(stderr, "warn: PMU is overcommitted and multiplexing "
                "is happening\n");
        warned = 1;
      }
      dutyCycle = (double) data.time_running / (double) data.time_enabled;
      counts[i] = (uint64_t)((double) data.value / dutyCycle);
    } else {
      counts[i] = data.value;
    }
  }

  for (i = 0; i < nr_events + 1; i++) {
    fprintf(out, "\t%" PRIu64, i == 0 ? counts[0] : counts[event_ptr[i-1]+1]);
  }
}

/* Dedup the set of events to count.
 *
 * The CPU cycles event is treated as an duplicate of the always-on
 * cycle counter.
 */
static int dedupEvents(int nr_events, int *events,
                       int *unique_events, int *event_ptr) {
  int i, j, k = 0;
  for (i = 0; i < nr_events; i++) {
    /* cycle counter */
    if (events[i] == 0x11) {
      event_ptr[i] = -1;
      continue;
    }
    for (j = 0; j < k; j++) {
      if (events[i] == unique_events[j])
        break;
    }
    if (j < k) {
      event_ptr[i] = j;
    } else {
      event_ptr[i] = k;
      unique_events[k++] = events[i];
    }
  }
  return k;
}

/* Initialize the performance counters to count a set of events.
 *
 * The cycle counter is always enabled.
 */
static void setupPerfEvents(struct perf_struct *pfs[], int cpu_begin,
                            int cpu_end, int n_events, int *events) {
  int i;
  for (i = cpu_begin; i < cpu_end; i++)
    pfs[i] = perf_initialize(i, n_events, events);
}

/* Write output CSV file header */
void writeCSVHeader(FILE *out, int n_cores, pmu_events_t events[]) {
  int i, j;
  fprintf(out, "Sample Count\tDelta Time\tTime Stamp\tTime Milliseconds");
  for (i = 0; i < n_cores; i++) {
    fprintf(out, "\tCore %d CycleCount", i);
    for (j = 0; j < events[i].nr_events; j++)
      fprintf(out, "\tCore %d Event 0x%02x", i, events[i].events[j]);
  }
  fprintf(out, "\n");
  fflush(out);
}

/* Signal handler */
static int done;

void signalHandler(int signo)
{
  if (signo == SIGINT || signo == SIGTERM)
    done = 1;
}

/* Show usage information */
static void showUsage()
{
  printf("\nUsage:\n"
         " datalog-recorder [options] [[<N_EVENTS> <E0> <E1> ...] ...]\n\n"
         "Options:\n"
         "  -o OUT_FILENAME   Output file name\n"
         "  -i INTERVAL       Interval between measurements (microseconds)\n");
}

/* Main */
int main(int argc, char *argv[]) {
  char *out_filename = "data.csv";
  int interval = 1000000;
  int c;
  int i, j;

  /* detect the number of CPUs */
  long n_cores = sysconf(_SC_NPROCESSORS_ONLN);

  pmu_events_t *events = calloc(n_cores, sizeof(pmu_events_t));
  if (events == NULL) {
    perror("calloc failed");
    exit(EXIT_FAILURE);
  }

  while ((c = getopt(argc, argv, "o:i:")) != -1)
    switch (c)
      {
      case 'o':
        out_filename = optarg;
        break;
      case 'i':
        interval = atoi(optarg);
        break;
      case '?':
        showUsage();
        return 1;
      default:
        abort();
      }

  /* Parse events from command line */
  i = optind;
  int core_id = 0;
  while (i < argc) {
    if (core_id >= n_cores) {
      fprintf(stderr, "More cores specified than in the system.\n");
      exit(EXIT_FAILURE);
    }
    int nr_events = atoi(argv[i]);
    if (nr_events < 0 || nr_events > MAX_CUSTOM_EVENTS ||
        i + nr_events >= argc) {
      fprintf(stderr, "Incorrect number of events specified.\n");
      exit(EXIT_FAILURE);
    }
    events[core_id].nr_events = nr_events;
    for (j = 0; j < nr_events; j++)
      events[core_id].events[j] = (int) strtol(argv[i + 1 + j], NULL, 16);
    core_id++;
    i += nr_events + 1;
  }

  printf(LOG_HEADER "Output file: %s\n", out_filename);
  printf(LOG_HEADER "Sampling interval: %d\n", interval);

  /* dedup events */
  for (core_id = 0; core_id < n_cores; core_id++) {
    events[core_id].nr_unique_events = dedupEvents(
        events[core_id].nr_events,
        events[core_id].events,
        events[core_id].unique_events,
        events[core_id].event_pointer);
    printf(LOG_HEADER "Found %d unique events for core %d\n",
           events[core_id].nr_unique_events, core_id);
  }

  /* write output CSV header */
  FILE *out = fopen(out_filename, "w");
  if (!out) {
    perror(LOG_HEADER "Fail to open output file");
    exit(EXIT_FAILURE);
  }
  writeCSVHeader(out, n_cores, events);

  printf(LOG_HEADER "Starting recording...\n");
  printf(LOG_HEADER "Setting up PMU events...\n");
  struct perf_struct **pfs =
      check_malloc(n_cores * sizeof(struct perf_struct *));

  for (i = 0; i < n_cores; i++) {
    printf(LOG_HEADER "Setting events for core %d: ", i);
    print_events(events[i].nr_unique_events, events[i].unique_events);
    setupPerfEvents(pfs, i, i + 1, events[i].nr_unique_events,
                    events[i].unique_events);
  }

  unsigned long n = 0;

  struct timeval currentTime, lastTime;
  double elapsedTime;
  gettimeofday(&lastTime, NULL);

  /* enable performance counters */
  for (i = 0; i < n_cores; i++) {
    int b = events[i].nr_unique_events;
    for (j = 0; j < b + 1; j++) {
      perf_resetcounter(pfs[i], j);
      perf_enablecounter(pfs[i], j);
    }
  }

  printf(LOG_HEADER "Start measurements...\n");

  /* Register signal handler */
  if (signal(SIGINT, signalHandler) == SIG_ERR ||
      signal(SIGTERM, signalHandler) == SIG_ERR) {
    perror("Cannot register signal handler");
    exit(EXIT_FAILURE);
  }

  /* main loop */
  while (!done) {
    /* get time stamp */
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char timeStamp[64];
    sprintf(timeStamp, "%d-%d-%d %d:%d:%d", tm.tm_year + 1900, tm.tm_mon + 1,
            tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    /* get sample time for last period */
    gettimeofday(&currentTime, NULL);
    elapsedTime = (currentTime.tv_sec - lastTime.tv_sec) * 1000.0;  /* sec to ms */
    elapsedTime +=
        (currentTime.tv_usec - lastTime.tv_usec) / 1000.0;  /* us to ms */
    elapsedTime /= 1000.0;
    lastTime = currentTime;

    struct timespec milt;
    clock_gettime(CLOCK_REALTIME, &milt);
    int64_t millitime = milt.tv_sec * INT64_C(1000) + milt.tv_nsec / 1000000;

    fprintf(out, "%lu\t%f\t%s\t%f", n, elapsedTime, timeStamp,
            (double)millitime);

    /* get perf counts */
    for (i = 0; i < n_cores; i++)
      appendCounts(out, pfs[i], events[i].nr_events,
                   events[i].nr_unique_events, events[i].event_pointer);

    fprintf(out, "\n");
    fflush(out);

    if (n % 100 == 0) printf(LOG_HEADER "Sample count: %lu\n", n);
    n++;

    usleep(interval);
  }

  /* disable performance counters */
  for (i = 0; i < n_cores; i++) {
    int b = events[i].nr_unique_events;
    for (j = 0; j < b + 1; j++) {
      perf_disablecounter(pfs[i], j);
    }
  }

  /* close performance counters and free data structures */
  for (i = 0; i < n_cores; i++)
    perf_close(pfs[i]);
  free(pfs);
  free(events);

  fclose(out);

  printf(LOG_HEADER "Finished :)\n");
}
