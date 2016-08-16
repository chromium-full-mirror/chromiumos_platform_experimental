CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++

SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))

CFLAGS = -g

all: datalog-recorder

datalog-recorder: datalog-recorder.o perf.o
	$(CC) -o $@ $^ -lrt

power-monitor: power-monitor.o perf.o dvfs.o
	$(CC) -o $@ $^ -lrt

perf_test: perf_test.o perf.o
	$(CC) -o $@ $^

test: perf_test
	taskset -c 0 ./perf_test 0

clean:
	rm -f $(OBJS) datalog-recorder perf_test power-monitor

.PHONY: clean
