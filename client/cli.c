
#include "cli.h"

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
    if (argc < 2) {
        printf("사용법:\n");
        printf("  ./client_app list\n");
        printf("  ./client_app restore <filename>\n");
        return -1;
    }

    // 백업 파일 목록 조회
    if (strcmp(argv[1], "list") == 0) {
        if (argc != 2) {
            printf("사용법: ./client_app list\n");
            return -1;
        }

        printf("[CLI] 백업 파일 목록 조회 요청\n");
        printf("[CLI] 서버 통신 기능은 아직 연결되지 않았습니다.\n");
    }

    // 파일 복구
    else if (strcmp(argv[1], "restore") == 0) {
        if (argc != 3) {
            printf("사용법: ./client_app restore <filename>\n");
            return -1;
        }

        printf("[CLI] 파일 복구 요청: %s\n", argv[2]);

        /*
         * 현재 서버 통신 및 실제 복구 기능이 연결되지 않았으므로
         * 복구와 해시 검증은 수행하지 않은 상태로 표시한다.
         *
         * 추후 서버에서 전달받은 실제 결과값으로 변경하면 된다.
         */
        RestoreResult result = {
            .restore_status = RESTORE_NOT_EXECUTED,
            .hash_status = HASH_NOT_CHECKED
        };

        cli_print_restore_result(&result);

        printf("[CLI] 서버 통신 기능은 아직 연결되지 않았습니다.\n");
    }

    // 잘못된 명령어
    else {
        printf("알 수 없는 명령어입니다: %s\n", argv[1]);
        printf("사용 가능한 명령어: list, restore\n");
        return -1;
    }

    return 0;
}

