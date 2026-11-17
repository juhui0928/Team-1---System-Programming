#define _POSIX_C_SOURCE 200809L

#include "remote.h"
#include <sys/inotify.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>

#include "cli.h"
#include "monitor.h"
#include "../common/queue.h"
#include "../common/logger.h"

// 작업 큐에서 파일 이벤트를 가져오는 스레드

// 의심스러운 파일 확장자 검사
static int suspicious_file(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (!ext)
        return 0;

    const char *blocked[] = {
        ".locked",
        ".encrypted",
        ".crypt",
        ".lockbit",
        ".wannacry",
        ".ransomware"
    };

    for (size_t i = 0;
         i < sizeof(blocked) / sizeof(blocked[0]);
         i++) {
        if (strcasecmp(ext, blocked[i]) == 0)
            return 1;
    }

    return 0;
}

// 작업 큐 소비 및 자동 원격 백업
static void *worker_thread(void *arg)
{
    WorkQueue *queue = (WorkQueue *)arg;
    WorkItem item;

    int backup_paused = 0;

    while (queue_pop(queue, &item) == 0) {

        if (item.event_mask &
            (IN_DELETE | IN_MOVED_FROM)) {

            logger_log("INFO",
                       "기존 서버 백업 유지: %s",
                       item.filename);
            continue;
        }

        if (suspicious_file(item.filename)) {

            backup_paused = 1;

            fprintf(stderr,
                    "[SECURITY] 의심 파일 감지: %s\n",
                    item.filename);

            fprintf(stderr,
                    "[SECURITY] 자동 백업 중단\n");

            logger_log("WARN",
                       "의심 파일 감지 및 백업 중단: %s",
                       item.filename);
            continue;
        }

        // 파일 쓰기가 완료된 경우에만 백업
        if (!(item.event_mask &
              (IN_CLOSE_WRITE | IN_MOVED_TO)))
            continue;

        if (backup_paused) {
            logger_log("WARN",
                       "보안 정책으로 백업 차단: %s",
                       item.filename);
            continue;
        }

        struct stat st;

        if (lstat(item.filename, &st) != 0 ||
            !S_ISREG(st.st_mode))
            continue;

        printf("[WORKER] 자동 백업 시작: %s\n",
               item.filename);

        if (remote_backup(item.filename) == 0) {

            logger_log("INFO",
                       "자동 원격 백업 성공: %s",
                       item.filename);

        } else {

            logger_log("ERROR",
                       "자동 원격 백업 실패: %s",
                       item.filename);
        }
    }

    return NULL;
}


// 파일 모니터링 실행
int run_monitor(const char *directory)
{
    WorkQueue queue;
    pthread_t monitor_tid;
    pthread_t worker_tid;

    atomic_int stop = 1;
    atomic_int status = 0;

    sigset_t signals;
    int signal_number;

    int monitor_created = 0;
    int worker_created = 0;
    int queue_initialized = 0;
    int result = -1;

    // 로그 파일 초기화
    if (logger_init("logs/cybervault.log") != 0) {
        fprintf(stderr, "[ERROR] 로그 파일 초기화 실패\n");
        return -1;
    }

    logger_log("INFO", "CyberVault 모니터링 프로그램 시작");

    // 종료 시그널 설정
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);

    // 스레드 생성 전에 종료 시그널 차단
    int mask_result = pthread_sigmask(SIG_BLOCK, &signals, NULL);

    if (mask_result != 0) {
        fprintf(stderr, "[ERROR] 시그널 설정 실패\n");
        logger_log("ERROR", "시그널 설정 실패: %s",
                   strerror(mask_result));
        goto cleanup;
    }

    // 작업 큐 초기화
    if (queue_init(&queue) != 0) {
        fprintf(stderr, "[ERROR] 큐 초기화 실패\n");
        logger_log("ERROR", "작업 큐 초기화 실패");
        goto cleanup;
    }

    queue_initialized = 1;

    // 작업 스레드 생성
    if (pthread_create(&worker_tid, NULL,
                       worker_thread, &queue) != 0) {
        fprintf(stderr, "[ERROR] 작업 스레드 생성 실패\n");
        logger_log("ERROR", "작업 스레드 생성 실패");
        goto cleanup;
    }

    worker_created = 1;

    // 모니터링 스레드에 전달할 정보
    MonitorArgs args = {
        .directory = directory,
        .queue = &queue,
        .stop = &stop,
        .status = &status
    };

    // 모니터링 스레드 생성
    if (pthread_create(&monitor_tid, NULL,
                       monitor_thread, &args) != 0) {
        fprintf(stderr, "[ERROR] 모니터링 스레드 생성 실패\n");
        logger_log("ERROR", "모니터링 스레드 생성 실패");
        goto cleanup;
    }

    monitor_created = 1;

    printf("[MAIN] 모니터링 초기화 대기 중...\n");

    // 모니터링 초기화 완료 또는 실패 대기
    while (atomic_load(&status) == 0) {

        struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = 100000000
        };

        nanosleep(&delay, NULL);
    }

    // 모니터링 초기화 실패
    if (atomic_load(&status) == -1) {
        fprintf(stderr, "[ERROR] 모니터링 초기화 실패\n");
        logger_log("ERROR", "모니터링 초기화 실패");
        goto cleanup;
    }

    printf("[MAIN] 파일 감시 실행 중: %s\n", directory);
    printf("[MAIN] 종료하려면 Ctrl+C를 누르세요.\n");

    logger_log("INFO", "파일 감시 실행 중: %s", directory);

    // SIGINT 또는 SIGTERM 대기
    int wait_result = sigwait(&signals, &signal_number);

    if (wait_result != 0) {
        fprintf(stderr, "[ERROR] 종료 시그널 대기 실패\n");
        logger_log("ERROR", "종료 시그널 대기 실패: %s",
                   strerror(wait_result));
        goto cleanup;
    }

    printf("\n[MAIN] 종료 신호 수신: %d\n", signal_number);
    logger_log("INFO", "종료 신호 수신: %d", signal_number);

    result = 0;

cleanup:

    // 모니터링 종료 요청
    atomic_store(&stop, 0);

    // 모니터링 스레드 종료 대기
    if (monitor_created) {
        pthread_join(monitor_tid, NULL);
    }

    // 큐 종료 및 대기 중인 스레드에 알림
    if (queue_initialized) {
        queue_close(&queue);
    }

    // 작업 스레드 종료 대기
    if (worker_created) {
        pthread_join(worker_tid, NULL);
    }

    // 큐 자원 해제
    if (queue_initialized) {
        queue_destroy(&queue);
    }

    logger_log("INFO", "CyberVault 모니터링 프로그램 종료");
    logger_close();

    printf("[MAIN] 프로그램 종료 완료\n");

    return result;
}

int main(int argc, char *argv[])
{
    // 파일 모니터링 명령어
    if (argc == 3 && strcmp(argv[1], "monitor") == 0) {
        return run_monitor(argv[2]) == 0 ? 0 : 1;
    }

    // 기존 CLI 명령어 실행
    if (cli_run(argc, argv) != 0) {
        return 1;
    }

    return 0;
}

