
#ifndef CLI_H
#define CLI_H

typedef enum {
    RESTORE_NOT_EXECUTED = 0,
    RESTORE_SUCCESS,
    RESTORE_FAILED
} RestoreStatus;

typedef enum {
    HASH_NOT_CHECKED = 0,
    HASH_VALID,
    HASH_INVALID
} HashStatus;

typedef struct {
    RestoreStatus restore_status;
    HashStatus hash_status;
} RestoreResult;

// CLI 명령어 처리
int cli_run(int argc, char *argv[]);

// 복구 및 해시 검증 결과 출력
void cli_print_restore_result(const RestoreResult *result);

#endif

