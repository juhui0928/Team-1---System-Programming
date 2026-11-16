#include "cli.h"

#include <stdio.h>
#include <string.h>

// CLI 명령어 처리
int cli_run(int argc, char *argv[])
{
    // 명령어가 입력되지 않은 경우
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
        printf("[CLI] 서버 통신 기능은 추후 연결 예정입니다.\n");
    }

    // 파일 복구
    else if (strcmp(argv[1], "restore") == 0) {
        if (argc != 3) {
            printf("사용법: ./client_app restore <filename>\n");
            return -1;
        }

        printf("[CLI] 파일 복구 요청: %s\n", argv[2]);
        printf("[CLI] 서버 통신 기능은 추후 연결 예정입니다.\n");
    }

    // 잘못된 명령어
    else {
        printf("알 수 없는 명령어입니다: %s\n", argv[1]);
        printf("사용 가능한 명령어: list, restore\n");
        return -1;
    }

    return 0;
}