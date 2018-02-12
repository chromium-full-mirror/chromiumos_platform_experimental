#!/usr/bin/python
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Checks kernel tracing events to get the FPS of a CrOS device.

This script requires root privilege to work properly. It may interfere
Chrome tracing because both use ftrace.

Limitation:
It doesn't support multi-screen.
It assumes 60 HZ screen refresh rate.

Sample output:
# ./fps_meter.py
trace method: workq
[111111111111111111111111111111111111111111111111111111111111] FPS: 60
[111111111111111111111111111111111111111111111111111111111111] FPS: 60
[11111111111111111111111111111111111111111111111 111111111111] FPS: 59
[111111111111111111111111111111111111111111111111111111111111] FPS: 60
[11111111111111 11111111111111111111 11111111 111111111111111] FPS: 57
[111111111111111111111111111111111    11111111111111111111111] FPS: 56
[111   111111111111111111111111111111111111111111111111111111] FPS: 57
 ^
 1 : Frame update count detected in this 1/60 sec interval.
"""
import argparse
import atexit
import collections
import os
import re
import threading
import time

TRACE_ROOT = '/sys/kernel/debug/tracing/'
VBLANK_SWITCH = os.path.join(TRACE_ROOT, 'events/drm/drm_vblank_event/enable')
FLIP_SWITCH = os.path.join(TRACE_ROOT, 'events/i915/i915_flip_complete/enable')
WORKQ_SWITCH = os.path.join(
    TRACE_ROOT, 'events/workqueue/workqueue_execute_start/enable')
WORKQ_FILTER = os.path.join(
    TRACE_ROOT, 'events/workqueue/workqueue_execute_start/filter')
TRACE_SWITCH = os.path.join(TRACE_ROOT, 'tracing_on')
TRACE_CLOCK = os.path.join(TRACE_ROOT, 'trace_clock')
TRACE_LOG = os.path.join(TRACE_ROOT, 'trace')
TRACE_PIPE = os.path.join(TRACE_ROOT, 'trace_pipe')
TRACE_MARKER = os.path.join(TRACE_ROOT, 'trace_marker')
REFRESH_RATE = 60
NOTIFY_STRING = 'notify_collection'


def is_intel_gpu():
  return os.path.isdir('/sys/bus/pci/drivers/i915')


def get_kernel_version():
  with open('/proc/version') as fin:
    m = re.match(r'Linux version (\d+.\d+).\d+', fin.read())
    if m:
      return m.group(1)
  return 'unknown'


def get_trace_method():
  """Gets the FPS checking method.

  Checks i915_flip_complete for Intel GPU on Kernel 3.18.
  Checks intel_atomic_commit_work for Intel GPU on Kernel 4.4.
  Checks drm_vblank_event otherwise.
  """
  if is_intel_gpu():
    kernel_version = get_kernel_version()
    if kernel_version == '4.4':
      return 'workq'
    elif kernel_version == '3.18':
      return 'flip'
  # Fallback.
  return 'vblank'


def write_to_file(content, filename):
  with open(filename, 'w') as fout:
    fout.write(content)


def set_simple_switch(value, filename):
  with open(filename) as fin:
    orig = fin.read().strip()
    atexit.register(write_to_file, orig, filename)
  write_to_file(value, filename)


def set_trace_clock():
  # Set trace clock to mono time as chrome tracing in CrOS.
  PREFERRED_TRACE_CLOCK = 'mono'
  with open(TRACE_CLOCK) as trace_clock:
    m = re.match(r'.*\[(\w+)\]', trace_clock.read())
    if m:
      orig_clock = m.group(1)
      atexit.register(write_to_file, orig_clock, TRACE_CLOCK)
  write_to_file(PREFERRED_TRACE_CLOCK, TRACE_CLOCK)


def get_kernel_symbol_addr(symbol):
  # Example line in kallsyms:
  # ffffffffbc46cb03 T sys_exit
  with open('/proc/kallsyms') as kallsyms:
    for line in kallsyms:
      items = line.split()
      if items[2] == symbol:
        addr = items[0]
        return addr
  return None


def set_workq_filter(function_name):
  addr = get_kernel_symbol_addr(function_name)
  if addr:
    filter = 'function == 0x%s' % addr
    write_to_file(filter, WORKQ_FILTER)
    # Sets to 0 to remove the filter.
    atexit.register(write_to_file, '0', WORKQ_FILTER)


def enable_tracing(trace_method):
  if trace_method == 'workq':
    set_simple_switch('1', WORKQ_SWITCH)
    # There are many workqueue_execute_start events,
    # filter to reduce CPU usage.
    set_workq_filter('intel_atomic_commit_work')
  elif trace_method == 'flip':
    set_simple_switch('1', FLIP_SWITCH)
  else:
    set_simple_switch('1', VBLANK_SWITCH)

  set_simple_switch('1', TRACE_SWITCH)
  set_trace_clock()


def notify_collection(period_sec=1.0):
  next_notify_time = time.time() + period_sec
  while True:
    # Calling time.sleep(1) may suspend for more than 1 second.
    # Sleeps until a specific time to avoid error accumulation.
    sleep_time = next_notify_time - time.time()
    next_notify_time += period_sec
    # Skips if current time is larger than next_notify_time.
    if sleep_time > 0:
      time.sleep(sleep_time)
      write_to_file(NOTIFY_STRING, TRACE_MARKER)


def start_notify_thread():
  notify_thread = threading.Thread(target=notify_collection)
  notify_thread.daemon = True
  notify_thread.start()


def process_trace_buffer(trace_buffer, end_time, verbose=False):
  # Checks all vblanks in the range [end_time - 1.0, end_time].
  frame_info = []
  step = 1.0 / REFRESH_RATE
  step_time = end_time - 1.0 + step
  fps_count = 0
  frame_times = []
  for _ in range(REFRESH_RATE):
    # Checks if there are vblanks in a refresh interval.
    step_count = 0
    while trace_buffer and trace_buffer[0] < step_time:
      frame_times.append(trace_buffer.popleft())
      step_count += 1

    # Each character represent an 1 / REFRESH_RATE interval.
    if step_count:
      if step_count >= 10:
        frame_info.append('*')
      else:
        frame_info.append(str(step_count))
      fps_count += 1
    else:
      frame_info.append(' ')
    step_time += step

  frame_info_str = ''.join(frame_info)
  print '[%s] FPS: %2d' % (frame_info_str, fps_count)
  if frame_times and verbose:
    print ', '.join(['%.3f' % t for t in frame_times])


def main_loop(trace_method, verbose=False):
  """Main loop to parse the trace.

  There are 2 threads involved:
  Main thread:
    Using blocking read to get data from trace_pipe.
  Notify thread: The main thread may wait indifinitely if there
    is no new trace. Writes to the pipe once per second to avoid
    the indefinite waiting.
  """
  start_notify_thread()

  # Sample trace:
  # <idle>-0  [000] dNh3 631.905961: drm_vblank_event: crtc=0, seq=65496
  # <idle>-0  [000] d.h3 631.922681: drm_vblank_event: crtc=0, seq=65497
  # fps_meter [003] ...1 632.393953: tracing_mark_write: notify_collection
  # ...
  re_notify = re.compile(
      r'.* (\d+.\d+): tracing_mark_write: ' + NOTIFY_STRING)
  if trace_method == 'workq':
    re_trace = re.compile(
        r'.* (\d+.\d+): workqueue_execute_start: work struct ([\da-f]+): '
        'function intel_atomic_commit_work')
  elif trace_method == 'flip':
    re_trace = re.compile(
        r'.* (\d+.\d+): i915_flip_complete: plane=(\d+), obj=([\da-f]+)')
  else:
    re_trace = re.compile(
        r'.* (\d+.\d+): drm_vblank_event: crtc=(\d+), seq=(\d+)')

  trace_buffer = collections.deque()
  with open(TRACE_PIPE) as trace_pipe:
    # The pipe may block a few seconds if using:
    # for line in trace_pipe
    while True:
      line = trace_pipe.readline()
      m_trace = re_trace.match(line)
      if m_trace:
        timestamp = float(m_trace.group(1))
        trace_buffer.append(timestamp)
      else:
        m_notify = re_notify.match(line)
        if m_notify:
          timestamp = float(m_notify.group(1))
          process_trace_buffer(trace_buffer, timestamp, verbose)


if __name__ == '__main__':
  # parsing arguments
  parser = argparse.ArgumentParser(description='Print fps infomation.')
  parser.add_argument('-v', dest='verbose', action='store_true',
                      help='print verbose frame time info')
  args = parser.parse_args()

  trace_method = get_trace_method()
  print 'trace method:', trace_method
  enable_tracing(trace_method)
  main_loop(trace_method, args.verbose)

