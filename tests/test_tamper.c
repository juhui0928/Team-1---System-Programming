
#include <stdio.h>
#include <unistd.h>
#include "../client/encryption.h"

int main(void)
{
    unsigned char key[AES_KEY_SIZE];

    for (int i = 0; i < AES_KEY_SIZE; i++)
        key[i] = (unsigned char)i;

    // 이전 테스트 결과 삭제
    remove("tampered_restored.txt");

    // 변조된 암호문 복호화 시도
    int result = decrypt_file(
        "tampered.enc",
        "tampered_restored.txt",
        key
    );

    if (result != -1) {
        printf("FAIL: 변조된 암호문 복호화 허용\n");
        return 1;
    }

    if (access("tampered_restored.txt", F_OK) == 0) {
        printf("FAIL: 인증 실패 파일이 남아 있음\n");
        return 1;
    }

    printf("PASS: 암호문 위변조 탐지 성공\n");
    printf("PASS: 변조된 파일 복구 차단\n");

    return 0;
}
