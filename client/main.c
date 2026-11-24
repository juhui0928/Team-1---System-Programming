
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
void *worker_thread(void *arg)
{
    WorkQueue *queue = (WorkQueue *)arg;
    WorkItem item;

    while (queue_pop(queue, &item) == 0) {

        printf("[WORKER] 파일 이벤트 처리: %s (event: 0x%x)\n",
               item.filename, item.event_mask);

        logger_log("INFO",
                   "파일 이벤트 처리: %s (event: 0x%x)",
                   item.filename, item.event_mask);
    }

    printf("[WORKER] 작업 스레드 종료\n");
    logger_log("INFO", "작업 스레드 종료");

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

