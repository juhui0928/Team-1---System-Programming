
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../client/encryption.h"
#include "../client/integrity.h"

int main(void)
{
    unsigned char key[AES_KEY_SIZE] = {0};
    char original_hash[SHA256_HEX_LENGTH];
    char restored_hash[SHA256_HEX_LENGTH];

    // 테스트 전용 고정 키 (실제 백업에서는 사용 금지)
    for (int i = 0; i < AES_KEY_SIZE; i++)
        key[i] = (unsigned char)i;

    // 테스트 파일 생성
    FILE *fp = fopen("sample.txt", "wb");
    if (!fp) return 1;

    fprintf(fp, "Hello Linux CyberVault!\n");
    fclose(fp);

    // 이전 테스트 결과 삭제
    remove("sample.enc");
    remove("sample_restored.txt");

    // 1. 원본 파일 암호화
    if (encrypt_file("sample.txt", "sample.enc", key) != 0) {
        printf("FAIL: 암호화 실패\n");
        return 1;
    }
    printf("PASS: AES-256-GCM 암호화 성공\n");

    // 2. 암호화 파일 복호화
    if (decrypt_file("sample.enc", "sample_restored.txt", key) != 0) {
        printf("FAIL: 복호화 실패\n");
        return 1;
    }
    printf("PASS: AES-256-GCM 복호화 성공\n");

    // 3. SHA-256으로 원본과 복구 파일 비교
    if (calculate_sha256("sample.txt", original_hash) != 0 ||
        calculate_sha256("sample_restored.txt", restored_hash) != 0) {
        printf("FAIL: SHA-256 계산 실패\n");
        return 1;
    }

    if (strcmp(original_hash, restored_hash) != 0) {
        printf("FAIL: 복구 파일이 원본과 다름\n");
        return 1;
    }
    printf("PASS: 원본과 복구 파일 SHA-256 일치\n");

    // 4. 잘못된 키로 복호화 시도
    unsigned char wrong_key[AES_KEY_SIZE] = {0};

    if (decrypt_file("sample.enc", "wrong_restored.txt",
                     wrong_key) == 0) {
        printf("FAIL: 잘못된 키를 허용함\n");
        return 1;
    }
    printf("PASS: 잘못된 키 복호화 차단\n");

    // 5. 인증 실패 파일이 남아 있지 않은지 확인
    fp = fopen("wrong_restored.txt", "rb");
    if (fp != NULL) {
        fclose(fp);
        printf("FAIL: 인증 실패 파일이 남아 있음\n");
        return 1;
    }
    printf("PASS: 인증 실패 파일 정리 확인\n");

    printf("\nALL TESTS PASSED\n");
    return 0;
}

