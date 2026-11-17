
#include "monitor.h"
#include "../common/logger.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <poll.h>
#include <sys/inotify.h>
#include <stdatomic.h>

void *monitor_thread(void *arg)
{
    MonitorArgs *args = (MonitorArgs *)arg;

    if (args == NULL || args->directory == NULL ||
        args->queue == NULL || args->stop == NULL ||
        args->status == NULL) {
        return NULL;
    }

    // inotify 초기화
    int fd = inotify_init();

    if (fd < 0) {
        perror("inotify_init");
        logger_log("ERROR", "inotify 초기화 실패");

        atomic_store(args->status, -1);
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
        logger_log("ERROR", "감시 디렉터리 등록 실패: %s",
                   args->directory);

        close(fd);
        atomic_store(args->status, -1);
        return NULL;
    }

    printf("[MONITOR] 감시 시작: %s\n", args->directory);
    logger_log("INFO", "파일 감시 시작: %s", args->directory);

    // 감시 시작 성공 알림
    atomic_store(args->status, 1);

    union {
        struct inotify_event alignment;
        char buffer[4096];
    } event_buffer;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    // 종료 요청이 들어올 때까지 감시
    while (atomic_load(args->stop)) {

        int ready = poll(&pfd, 1, 500);

        if (ready < 0) {
            if (errno == EINTR)
                continue;

            perror("poll");
            logger_log("ERROR", "파일 감시 poll 오류");
            break;
        }

        if (ready == 0)
            continue;

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "[MONITOR] inotify 오류\n");
            logger_log("ERROR", "inotify 이벤트 감지 오류");
            break;
        }

        if (pfd.revents & POLLIN) {

            ssize_t length = read(fd, event_buffer.buffer,
                                  sizeof(event_buffer.buffer));

            if (length < 0) {
                if (errno == EINTR)
                    continue;

                perror("inotify read");
                logger_log("ERROR", "inotify read 오류");
                break;
            }

            ssize_t i = 0;

            while (i + (ssize_t)sizeof(struct inotify_event) <= length) {

                struct inotify_event *event =
                    (struct inotify_event *)&event_buffer.buffer[i];

                size_t event_size =
                    sizeof(struct inotify_event) + event->len;

                if (event_size > (size_t)(length - i))
                    break;

                if (event->len > 0 && !(event->mask & IN_ISDIR)) {

                    char filepath[PATH_MAX];

                    int result = snprintf(filepath, sizeof(filepath),
                                           "%s/%s",
                                           args->directory, event->name);

                    if (result >= 0 &&
                        (size_t)result < sizeof(filepath)) {

                        if (event->mask & IN_CREATE) {
                            printf("[MONITOR] 파일 생성: %s\n", filepath);
                            logger_log("INFO", "파일 생성: %s", filepath);
                        }

                        if (event->mask & IN_CLOSE_WRITE) {
                            printf("[MONITOR] 파일 수정: %s\n", filepath);
                            logger_log("INFO", "파일 수정: %s", filepath);
                        }

                        if (event->mask & IN_DELETE) {
                            printf("[MONITOR] 파일 삭제: %s\n", filepath);
                            logger_log("INFO", "파일 삭제: %s", filepath);
                        }

                        if (event->mask & IN_MOVED_FROM) {
                            printf("[MONITOR] 파일 이동: %s\n", filepath);
                            logger_log("INFO", "파일 이동: %s", filepath);
                        }

                        if (event->mask & IN_MOVED_TO) {
                            printf("[MONITOR] 파일 이동: %s\n", filepath);
                            logger_log("INFO", "파일 이동: %s", filepath);
                        }

                        // 작업 큐에 이벤트 전달
                        if (queue_push(args->queue, filepath,
                                       event->mask) != 0) {
                            printf("[MONITOR] 큐에 작업 추가 실패\n");

                            logger_log("ERROR",
                                       "작업 큐 추가 실패: %s", filepath);
                        }
                    }
                }

                i += event_size;
            }
        }
    }

    // 감시 종료 및 자원 해제
    inotify_rm_watch(fd, wd);
    close(fd);

    printf("[MONITOR] 감시 종료\n");
    logger_log("INFO", "파일 감시 종료");

    return NULL;
}

