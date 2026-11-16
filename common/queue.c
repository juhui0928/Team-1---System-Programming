
#include "queue.h"

#include <stdio.h>
#include <string.h>

// 큐 초기화
int queue_init(WorkQueue *queue)
{
    if (queue == NULL)
        return -1;

    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
    queue->closed = 0;

    if (pthread_mutex_init(&queue->mutex, NULL) != 0)
        return -1;

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }

    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }

    return 0;
}

// 작업 큐에 파일 정보 추가
int queue_push(WorkQueue *queue,
               const char *filename,
               uint32_t event_mask)
{
    if (queue == NULL || filename == NULL)
        return -1;

    pthread_mutex_lock(&queue->mutex);

    // 큐가 가득 찬 경우 빈 공간이 생길 때까지 대기
    while (queue->count == QUEUE_SIZE && !queue->closed)
        pthread_cond_wait(&queue->not_full, &queue->mutex);

    if (queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    strncpy(queue->items[queue->rear].filename,
            filename, MAX_FILENAME - 1);

    queue->items[queue->rear].filename[MAX_FILENAME - 1] = '\0';
    queue->items[queue->rear].event_mask = event_mask;

    queue->rear = (queue->rear + 1) % QUEUE_SIZE;
    queue->count++;

    // 대기 중인 소비자 스레드에 알림
    pthread_cond_signal(&queue->not_empty);

    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

// 큐에서 작업 가져오기
int queue_pop(WorkQueue *queue, WorkItem *item)
{
    if (queue == NULL || item == NULL)
        return -1;

    pthread_mutex_lock(&queue->mutex);

    // 큐가 비어 있으면 작업이 들어올 때까지 대기
    while (queue->count == 0 && !queue->closed)
        pthread_cond_wait(&queue->not_empty, &queue->mutex);

    // 종료되었고 남은 작업도 없는 경우
    if (queue->count == 0 && queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    *item = queue->items[queue->front];

    queue->front = (queue->front + 1) % QUEUE_SIZE;
    queue->count--;

    // 대기 중인 생산자 스레드에 알림
    pthread_cond_signal(&queue->not_full);

    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

// 큐 종료
void queue_close(WorkQueue *queue)
{
    if (queue == NULL)
        return;

    pthread_mutex_lock(&queue->mutex);

    queue->closed = 1;

    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);

    pthread_mutex_unlock(&queue->mutex);
}

// 큐 자원 해제
void queue_destroy(WorkQueue *queue)
{
    if (queue == NULL)
        return;

    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->mutex);
}
