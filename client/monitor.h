
#ifndef MONITOR_H
#define MONITOR_H

#include "../common/queue.h"
#include <stdatomic.h>

typedef struct {
    const char *directory;
    WorkQueue *queue;
    atomic_int *stop;
    atomic_int *status;
} MonitorArgs;

void *monitor_thread(void *arg);

#endif

