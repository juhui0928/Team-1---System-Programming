
#include "monitor.h"

#include <stdio.h>
#include <pthread.h>
#include <signal.h>

// 종료 요청을 저장하는 변수
static volatile sig_atomic_t stop_requested = 1;

// Ctrl+C 입력 시 호출되는 함수
void handle_signal(int signal)
{
    (void)signal;
    stop_requested = 0;
}

int main(void)
{
    WorkQueue queue;
    pthread_t monitor_tid;

    // Ctrl+C 신호 처리 등록
    signal(SIGINT, handle_signal);

    // 작업 큐 초기화
    if (queue_init(&queue) != 0) {
        printf("Queue initialization failed\n");
        return 1;
    }

    // 감시할 폴더 설정
    MonitorArgs args = {
        .directory = "/home/jungj/monitor_test",
        .queue = &queue,
        .stop = &stop_requested
    };

    // 파일 감시 스레드 생성
    if (pthread_create(&monitor_tid, NULL,
                       monitor_thread, &args) != 0) {
        printf("Monitor thread creation failed\n");
        queue_destroy(&queue);
        return 1;
    }

    printf("파일 감시 및 작업 큐 테스트 시작!\n");
    printf("종료하려면 Ctrl+C를 누르세요.\n");

    WorkItem item;

    // 큐에서 작업을 꺼내 출력
    while (queue_pop(&queue, &item) == 0) {
        printf("[QUEUE] 파일 경로: %s\n", item.filename);
        printf("[QUEUE] 이벤트 정보: 0x%x\n", item.event_mask);
    }

    // 모니터링 스레드 종료 대기
    pthread_join(monitor_tid, NULL);

    // 큐 자원 해제
    queue_destroy(&queue);

    printf("프로그램이 정상적으로 종료되었습니다.\n");

    return 0;
}
