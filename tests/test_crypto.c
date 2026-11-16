
#include <stdio.h>
#include "../client/integrity.h"

int main(void)
{
    char hash[SHA256_HEX_LENGTH];

    // 테스트 파일 생성
    FILE *fp = fopen("test1.txt", "w");
    if (fp == NULL)
        return 1;

    fprintf(fp, "Hello CyberVault");
    fclose(fp);

    // SHA-256 해시값 계산
    if (calculate_sha256("test1.txt", hash) != 0) {
        printf("SHA-256 계산 실패\n");
        return 1;
    }

    printf("SHA-256: %s\n", hash);

    // 같은 내용의 두 파일 비교
    fp = fopen("test2.txt", "w");
    if (fp == NULL)
        return 1;

    fprintf(fp, "Hello CyberVault");
    fclose(fp);

    int result = verify_file_integrity("test1.txt", "test2.txt");

    if (result == 1)
        printf("무결성 검사 성공: 두 파일이 동일합니다.\n");
    else
        printf("무결성 검사 실패\n");

    return result == 1 ? 0 : 1;
}

