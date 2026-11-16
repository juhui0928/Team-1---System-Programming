
#ifndef MONITOR_H
#define MONITOR_H

#include "../common/queue.h"
#include <signal.h>

// 파일 감시에 필요한 정보
typedef struct {
    const char *directory;
    WorkQueue *queue;
    volatile sig_atomic_t *stop;
} MonitorArgs;

// 파일 감시 스레드
void *monitor_thread(void *arg);

#endif
