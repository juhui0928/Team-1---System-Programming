
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../client/integrity.h"
#include "../client/encryption.h"
#include "../client/recovery.h"

int main(void)
{
    unsigned char key[AES_KEY_SIZE];
    char hash[SHA256_HEX_LENGTH];

    // 테스트 전용 키
    for (int i = 0; i < AES_KEY_SIZE; i++)
        key[i] = (unsigned char)i;

    remove("large_test.enc");
    remove("large_restored.bin");

    // 원본 파일 해시 계산
    if (calculate_sha256("large_test.bin", hash) != 0) {
        printf("FAIL: 해시 계산\n");
        return 1;
    }

    // 10MB 파일 암호화
    if (encrypt_file("large_test.bin", "large_test.enc", key) != 0) {
        printf("FAIL: 대용량 암호화\n");
        return 1;
    }
    printf("PASS: 10MB 암호화\n");

    // 암호화된 파일 복구
    if (recover_file("large_test.enc", "large_restored.bin",
                     key, hash) != 0) {
        printf("FAIL: 대용량 복구\n");
        return 1;
    }
    printf("PASS: 10MB 복구\n");

    // SHA-256 무결성 비교
    if (verify_file_integrity("large_test.bin",
                              "large_restored.bin") != 1) {
        printf("FAIL: 무결성 불일치\n");
        return 1;
    }
    printf("PASS: 10MB SHA-256 일치\n");

    printf("\nALL LARGE FILE TESTS PASSED\n");
    return 0;
}
