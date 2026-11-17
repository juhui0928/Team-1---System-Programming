#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include "../client/backup.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        printf("FAIL: %s\n", message); \
        return 1; \
    } \
    printf("PASS: %s\n", message); \
} while (0)

int main(void)
{
    unsigned char key[AES_KEY_SIZE] = {0};
    char hash1[SHA256_HEX_LENGTH];
    char hash2[SHA256_HEX_LENGTH];
    char saved_hash[SHA256_HEX_LENGTH];
    char restored_hash[SHA256_HEX_LENGTH];

    FILE *fp = fopen("sample.txt", "w");
    CHECK(fp != NULL, "테스트 파일 생성");
    fputs("CyberVault Version 1\n", fp);
    fclose(fp);

    CHECK(backup_file("sample.txt", key, hash1) == 0,
          "첫 번째 암호화 백업");

    fp = fopen("sample.txt", "w");
    CHECK(fp != NULL, "원본 파일 수정");
    fputs("CyberVault Version 2\n", fp);
    fclose(fp);

    CHECK(backup_file("sample.txt", key, hash2) == 0,
          "두 번째 암호화 백업");

    CHECK(strcmp(hash1, hash2) != 0,
          "변경된 파일 해시 구분");

    glob_t files = {0};

    CHECK(glob("backups/*/sample.txt.enc",
               0, NULL, &files) == 0,
          "백업 파일 검색");

    CHECK(files.gl_pathc == 2,
          "독립적인 백업 버전 2개 유지");

    int found1 = 0;
    int found2 = 0;

    for (size_t i = 0; i < files.gl_pathc; i++) {
        CHECK(load_backup_hash(files.gl_pathv[i],
                               saved_hash) == 0,
              "저장된 SHA-256 불러오기");

        if (strcmp(saved_hash, hash1) == 0)
            found1++;
        if (strcmp(saved_hash, hash2) == 0)
            found2++;

        char restored[128];

        snprintf(restored, sizeof(restored),
                 "restored_%zu.txt", i);

        CHECK(restore_backup(files.gl_pathv[i],
                             restored, key) == 0,
              "백업 버전 복구");

        CHECK(calculate_sha256(restored,
                               restored_hash) == 0,
              "복구 파일 해시 계산");

        CHECK(strcmp(saved_hash, restored_hash) == 0,
              "복구 파일 무결성 검증");

        CHECK(restore_backup(files.gl_pathv[i],
                             restored, key) != 0,
              "기존 복구 파일 덮어쓰기 방지");

        unsigned char wrong_key[AES_KEY_SIZE] = {1};

        CHECK(restore_backup(files.gl_pathv[i],
                             "wrong_key.txt",
                             wrong_key) != 0,
              "잘못된 암호화 키 차단");
    }

    CHECK(found1 == 1 && found2 == 1,
          "두 버전의 원본 해시 보존");

    globfree(&files);

    printf("\nALL BACKUP TESTS PASSED\n");
    return 0;
}
