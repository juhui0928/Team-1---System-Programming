
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../client/recovery.h"
#include "../client/integrity.h"
#include "../client/encryption.h"

int main(void)
{
    unsigned char key[AES_KEY_SIZE];
    char original_hash[SHA256_HEX_LENGTH];

    // 테스트용 암호화 키
    for (int i = 0; i < AES_KEY_SIZE; i++)
        key[i] = (unsigned char)i;

    // 이전 테스트 파일 정리
    remove("recovery_test.enc");
    remove("recovery_restored.txt");

    // 1. 원본 파일 생성
    FILE *fp = fopen("recovery_original.txt", "wb");
    if (!fp) return 1;

    fprintf(fp, "Linux CyberVault Recovery Test\n");
    fclose(fp);

    // 2. 원본 파일 해시 계산
    if (calculate_sha256("recovery_original.txt",
                         original_hash) != 0)
        return 1;

    // 3. 원본 파일 암호화
    if (encrypt_file("recovery_original.txt",
                     "recovery_test.enc", key) != 0) {
        printf("FAIL: 암호화 실패\n");
        return 1;
    }
    printf("PASS: 백업 파일 암호화\n");

    // 4. 암호화된 파일 복구
    if (recover_file("recovery_test.enc",
                     "recovery_restored.txt",
                     key, original_hash) != 0) {
        printf("FAIL: 파일 복구 실패\n");
        return 1;
    }
    printf("PASS: 파일 복구 성공\n");

    // 5. 복구된 파일의 무결성 검사
    int result = verify_file_integrity(
        "recovery_original.txt",
        "recovery_restored.txt"
    );

    if (result != 1) {
        printf("FAIL: 복구 파일 무결성 불일치\n");
        return 1;
    }
    printf("PASS: 복구 파일 SHA-256 일치\n");

    // 6. 기존 파일 덮어쓰기 방지
    if (recover_file("recovery_test.enc",
                     "recovery_restored.txt",
                     key, original_hash) == 0) {
        printf("FAIL: 기존 파일 덮어쓰기 허용\n");
        return 1;
    }
    printf("PASS: 기존 파일 덮어쓰기 차단\n");

    // 7. 잘못된 해시값으로 복구 시도
    char wrong_hash[SHA256_HEX_LENGTH];
    memset(wrong_hash, '0', 64);
    wrong_hash[64] = '\0';

    remove("recovery_wrong.txt");

    if (recover_file("recovery_test.enc",
                     "recovery_wrong.txt",
                     key, wrong_hash) == 0) {
        printf("FAIL: 잘못된 해시값 허용\n");
        return 1;
    }

    if (access("recovery_wrong.txt", F_OK) == 0) {
        printf("FAIL: 검증 실패 파일이 남아 있음\n");
        return 1;
    }
    printf("PASS: 잘못된 해시값 복구 차단\n");

    printf("\nALL RECOVERY TESTS PASSED\n");
    return 0;
}
