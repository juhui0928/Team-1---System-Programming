
#include "monitor.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <poll.h>
#include <sys/inotify.h>

// 파일 감시 스레드
void *monitor_thread(void *arg)
{
    MonitorArgs *args = (MonitorArgs *)arg;

    if (args == NULL || args->directory == NULL ||
        args->queue == NULL || args->stop == NULL)
        return NULL;

    // inotify 초기화
    int fd = inotify_init();

    if (fd < 0) {
        perror("inotify_init");
        return NULL;
    }

    uint32_t watch_events =
        IN_CREATE |
        IN_CLOSE_WRITE |
        IN_DELETE |
        IN_MOVED_FROM |
        IN_MOVED_TO;

    // 감시 대상 디렉터리 등록
    int wd = inotify_add_watch(fd, args->directory, watch_events);

    if (wd < 0) {
        perror("inotify_add_watch");
        close(fd);
        return NULL;
    }

    printf("[MONITOR] 감시 시작: %s\n", args->directory);

    union {
        struct inotify_event alignment;
        char buffer[4096];
    } event_buffer;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    // 종료 요청이 들어올 때까지 감시
    while (*(args->stop)) {

        int ready = poll(&pfd, 1, 500);

        if (ready < 0) {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        // 이벤트가 없으면 종료 여부를 다시 확인
        if (ready == 0)
            continue;

        if (pfd.revents & POLLIN) {
            int length = read(fd, event_buffer.buffer,
                              sizeof(event_buffer.buffer));

            if (length < 0) {
                if (errno == EINTR)
                    continue;

                perror("inotify read");
                break;
            }

            int i = 0;

            while (i < length) {
                struct inotify_event *event =
                    (struct inotify_event *)&event_buffer.buffer[i];

                if (event->len > 0 && !(event->mask & IN_ISDIR)) {
                    char filepath[PATH_MAX];

                    int result = snprintf(filepath, sizeof(filepath),
                                           "%s/%s",
                                           args->directory, event->name);

                    if (result >= 0 &&
                        (size_t)result < sizeof(filepath)) {

                        if (event->mask & IN_CREATE)
                            printf("[MONITOR] 파일 생성: %s\n", filepath);

                        if (event->mask & IN_CLOSE_WRITE)
                            printf("[MONITOR] 파일 수정: %s\n", filepath);

                        if (event->mask & IN_DELETE)
                            printf("[MONITOR] 파일 삭제: %s\n", filepath);

                        if (event->mask & IN_MOVED_FROM)
                            printf("[MONITOR] 파일 이동: %s\n", filepath);

                        if (event->mask & IN_MOVED_TO)
                            printf("[MONITOR] 파일 이동: %s\n", filepath);

                        if (queue_push(args->queue, filepath,
                                       event->mask) != 0) {
                            printf("[MONITOR] 큐에 작업 추가 실패\n");
                        }
                    }
                }

                i += sizeof(struct inotify_event) + event->len;
            }
        }
    }

    // 감시 종료 및 자원 해제
    inotify_rm_watch(fd, wd);
    close(fd);

    // 작업 큐에 종료 알림
    queue_close(args->queue);

    printf("[MONITOR] 감시 종료\n");

    return NULL;
}
