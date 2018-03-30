// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char** argv) {
  char* buf;
  size_t i, size;
  if (argc < 2) {
    fprintf(stderr, "Usage: ./memory-eater-locked <size in MB>\n");
    return 1;
  }
  if (mlockall(MCL_FUTURE)) {
    fprintf(stderr, "Failed to lock pages: %d\n", errno);
    return 1;
  }
  size = strtol(argv[1], NULL, 10);
  size *= 1024 * 1024;
  printf("Allocating %zd bytes\n", size);
  buf = (char*) malloc(size);
  memset(buf, 'x', size);
  printf("Done\n");
  fflush(stdout);
  pause();
  return 0;
}
