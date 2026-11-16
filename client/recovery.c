
#define _POSIX_C_SOURCE 200809L

#include "recovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 암호화된 백업 파일을 안전하게 복구하는 함수
int recover_file(
    const char *encrypted_path,
    const char *restored_path,
    const unsigned char key[AES_KEY_SIZE],
    const char expected_hash[SHA256_HEX_LENGTH])
{
    if (!encrypted_path || !restored_path ||
        !key || !expected_hash)
        return -1;

    // SHA-256 해시 문자열 길이 검사
    if (strlen(expected_hash) != SHA256_HEX_LENGTH - 1)
        return -1;

    // 복구 대상과 같은 디렉터리에 임시 작업 공간 생성
    size_t path_len = strlen(restored_path);
    size_t dir_size = path_len + 32;

    char *temp_dir = malloc(dir_size);
    if (!temp_dir)
        return -1;

    snprintf(temp_dir, dir_size,
             "%s.recovery-XXXXXX", restored_path);

    if (!mkdtemp(temp_dir)) {
        free(temp_dir);
        return -1;
    }

    // 임시 복호화 파일 경로 생성
    size_t temp_size = strlen(temp_dir) + 16;
    char *temp_path = malloc(temp_size);

    if (!temp_path) {
        rmdir(temp_dir);
        free(temp_dir);
        return -1;
    }

    snprintf(temp_path, temp_size,
             "%s/payload", temp_dir);

    int result = -1;
    char actual_hash[SHA256_HEX_LENGTH];

    // 1. 암호화된 백업 파일 복호화
    if (decrypt_file(encrypted_path, temp_path, key) != 0) {
        fprintf(stderr, "복호화 실패\n");
        goto cleanup;
    }

    // 2. 복호화된 파일의 SHA-256 계산
    if (calculate_sha256(temp_path, actual_hash) != 0) {
        fprintf(stderr, "해시 계산 실패\n");
        goto cleanup;
    }

    // 3. 원본 해시값과 비교
    if (strcmp(actual_hash, expected_hash) != 0) {
        fprintf(stderr, "무결성 검증 실패\n");
        goto cleanup;
    }

    // 4. 검증된 파일만 최종 복구
    // link()는 대상이 이미 존재하면 덮어쓰지 않음
    if (link(temp_path, restored_path) != 0) {
        perror("복구 파일 생성 실패");
        goto cleanup;
    }

    result = 0;

cleanup:
    // 임시 파일 및 디렉터리 정리
    unlink(temp_path);
    rmdir(temp_dir);

    free(temp_path);
    free(temp_dir);

    return result;
}
