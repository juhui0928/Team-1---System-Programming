
#include <stdio.h>
#include <stdlib.h>
#include "../client/integrity.h"
#include "../client/encryption.h"
#include "../client/recovery.h"

int main(void)
{
    unsigned char key[AES_KEY_SIZE] = {0};
    char hash[SHA256_HEX_LENGTH];

    remove("empty_test.enc");
    remove("empty_restored.txt");

    if (calculate_sha256("empty_test.txt", hash) != 0)
        return 1;

    if (encrypt_file("empty_test.txt", "empty_test.enc", key) != 0) {
        printf("FAIL: 빈 파일 암호화\n");
        return 1;
    }
    printf("PASS: 빈 파일 암호화\n");

    if (recover_file("empty_test.enc", "empty_restored.txt",
                     key, hash) != 0) {
        printf("FAIL: 빈 파일 복구\n");
        return 1;
    }
    printf("PASS: 빈 파일 복구\n");

    if (verify_file_integrity("empty_test.txt",
                              "empty_restored.txt") != 1) {
        printf("FAIL: 빈 파일 무결성\n");
        return 1;
    }
    printf("PASS: 빈 파일 SHA-256 일치\n");

    printf("\nALL EMPTY FILE TESTS PASSED\n");
    return 0;
}
