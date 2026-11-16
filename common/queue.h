
#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stdint.h>

#define QUEUE_SIZE 64
#define MAX_FILENAME 256

// 큐에 저장할 작업 정보
typedef struct {
    char filename[MAX_FILENAME];
    uint32_t event_mask;
} WorkItem;

// 작업 큐 구조체
typedef struct {
    WorkItem items[QUEUE_SIZE];

    int front;
    int rear;
    int count;
    int closed;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} WorkQueue;

// 큐 초기화
int queue_init(WorkQueue *queue);

// 작업 추가
int queue_push(WorkQueue *queue,
               const char *filename,
               uint32_t event_mask);

// 작업 가져오기
int queue_pop(WorkQueue *queue, WorkItem *item);

// 큐 종료
void queue_close(WorkQueue *queue);

// 큐 자원 해제
void queue_destroy(WorkQueue *queue);

#endif
