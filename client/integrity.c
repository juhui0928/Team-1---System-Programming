
#include "integrity.h"

#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>

// 파일의 SHA-256 해시값 계산
int calculate_sha256(const char *filepath,
                     char output[SHA256_HEX_LENGTH])
{
    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        perror("파일 열기 실패");
        return -1;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        fclose(fp);
        return -1;
    }

    unsigned char buffer[4096];
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    size_t bytes;
    int result = -1;

    // SHA-256 해시 계산 초기화
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
        goto cleanup;

    // 파일을 4096바이트씩 읽어 해시 계산
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, bytes) != 1)
            goto cleanup;
    }

    // 파일 읽기 오류 확인
    if (ferror(fp))
        goto cleanup;

    // 최종 해시값 생성
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1)
        goto cleanup;

    if (hash_len != 32)
        goto cleanup;

    // 해시값을 16진수 문자열로 변환
    for (unsigned int i = 0; i < hash_len; i++) {
        sprintf(output + i * 2, "%02x", hash[i]);
    }

    output[64] = '\0';
    result = 0;

cleanup:
    EVP_MD_CTX_free(ctx);
    fclose(fp);
    return result;
}

// 두 파일의 무결성 비교
int verify_file_integrity(const char *file1,
                          const char *file2)
{
    char hash1[SHA256_HEX_LENGTH];
    char hash2[SHA256_HEX_LENGTH];

    if (calculate_sha256(file1, hash1) != 0)
        return -1;

    if (calculate_sha256(file2, hash2) != 0)
        return -1;

    return strcmp(hash1, hash2) == 0 ? 1 : 0;
}
