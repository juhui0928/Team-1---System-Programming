
#include "cli.h"
#include "remote.h"
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <stdio.h>
#include <string.h>

// 복구 및 해시 검증 결과 출력
void cli_print_restore_result(const RestoreResult *result)
{
    if (result == NULL) {
        printf("[ERROR] 복구 결과가 없습니다.\n");
        return;
    }

    printf("\n========== 복구 결과 ==========\n");

    switch (result->restore_status) {
    case RESTORE_SUCCESS:
        printf("[RESTORE] 파일 복구 성공\n");
        break;

    case RESTORE_FAILED:
        printf("[RESTORE] 파일 복구 실패\n");
        break;

    case RESTORE_NOT_EXECUTED:
    default:
        printf("[RESTORE] 복구가 실행되지 않았습니다.\n");
        break;
    }

    switch (result->hash_status) {
    case HASH_VALID:
        printf("[HASH] SHA-256 검증 성공\n");
        break;

    case HASH_INVALID:
        printf("[HASH] SHA-256 검증 실패\n");
        break;

    case HASH_NOT_CHECKED:
    default:
        printf("[HASH] SHA-256 검증이 수행되지 않았습니다.\n");
        break;
    }

    printf("===============================\n");
}

// CLI 명령어 처리

int cli_run(int argc, char *argv[])
{
    if (argc == 3 && strcmp(argv[1], "backup") == 0)
        return remote_backup(argv[2]);

    if (strcmp(argc > 1 ? argv[1] : "", "list") == 0) {
        if (argc == 2)
            return remote_list(NULL);
        if (argc == 3)
            return remote_list(argv[2]);
    }

    if (argc >= 3 && argc <= 5 &&
        strcmp(argv[1], "restore") == 0) {

        char output[512];
        unsigned int version = 0;

        if (argc >= 4) {
            if (strlen(argv[3]) >= sizeof(output))
                return -1;
            strcpy(output, argv[3]);
        } else {
            int n = snprintf(output, sizeof(output),
                             "%s.restored", argv[2]);

            if (n < 0 || (size_t)n >= sizeof(output))
                return -1;
        }

        if (argc == 5) {
            char *end;
            errno = 0;
            unsigned long v = strtoul(argv[4], &end, 10);

            if (errno || end == argv[4] || *end ||
                argv[4][0] == '-' || v > UINT32_MAX)
                return -1;

            version = (unsigned int)v;
        }

        return remote_restore(argv[2], output, version);
    }

    printf("사용법:\n");
    printf("  ./client_app backup <file>\n");
    printf("  ./client_app list [filename]\n");
    printf("  ./client_app restore <filename> [output] [version]\n");
    printf("  ./client_app monitor <directory>\n");

    return -1;
}
