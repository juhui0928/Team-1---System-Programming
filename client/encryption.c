
#include "encryption.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

// 모든 바이트를 파일에 기록
static int write_all(FILE *fp, const unsigned char *buf, size_t len)
{
    return fwrite(buf, 1, len, fp) == len ? 0 : -1;
}

// AES-256-GCM 암호화
int encrypt_file(const char *input_path,
                 const char *output_path,
                 const unsigned char key[AES_KEY_SIZE])
{
    if (!input_path || !output_path || !key ||
        strcmp(input_path, output_path) == 0)
        return -1;

    FILE *in = fopen(input_path, "rb");
    if (!in) return -1;

    // 기존 출력 파일을 덮어쓰지 않도록 새 파일만 생성
    FILE *out = fopen(output_path, "wbx");
    if (!out) {
        fclose(in);
        return -1;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char iv[AES_IV_SIZE];
    unsigned char tag[AES_TAG_SIZE];
    unsigned char input[BUFFER_SIZE];
    unsigned char output[BUFFER_SIZE + 16];
    size_t n;
    int len;
    int ok = 0;

    if (!ctx) goto cleanup;

    // 암호학적으로 안전한 랜덤 IV 생성
    if (RAND_bytes(iv, sizeof(iv)) != 1) goto cleanup;
    if (write_all(out, iv, sizeof(iv)) != 0) goto cleanup;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
        goto cleanup;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                            AES_IV_SIZE, NULL) != 1)
        goto cleanup;

    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1)
        goto cleanup;

    // 파일 내용을 조금씩 암호화
    while ((n = fread(input, 1, sizeof(input), in)) > 0) {
        if (EVP_EncryptUpdate(ctx, output, &len, input, (int)n) != 1)
            goto cleanup;
        if (write_all(out, output, (size_t)len) != 0)
            goto cleanup;
    }

    if (ferror(in)) goto cleanup;

    if (EVP_EncryptFinal_ex(ctx, output, &len) != 1)
        goto cleanup;
    if (write_all(out, output, (size_t)len) != 0)
        goto cleanup;

    // 인증 태그 생성 및 저장
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                            AES_TAG_SIZE, tag) != 1)
        goto cleanup;

    if (write_all(out, tag, sizeof(tag)) != 0)
        goto cleanup;

    if (fflush(out) != 0 || fsync(fileno(out)) != 0)
        goto cleanup;

    ok = 1;

cleanup:
    EVP_CIPHER_CTX_free(ctx);
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok) remove(output_path);

    return ok ? 0 : -1;
}

// AES-256-GCM 복호화
int decrypt_file(const char *input_path,
                 const char *output_path,
                 const unsigned char key[AES_KEY_SIZE])
{
    if (!input_path || !output_path || !key ||
        strcmp(input_path, output_path) == 0)
        return -1;

    FILE *in = fopen(input_path, "rb");
    if (!in) return -1;

    if (fseek(in, 0, SEEK_END) != 0) {
        fclose(in);
        return -1;
    }

    long file_size = ftell(in);
    if (file_size < AES_IV_SIZE + AES_TAG_SIZE ||
        fseek(in, 0, SEEK_SET) != 0) {
        fclose(in);
        return -1;
    }

    unsigned char iv[AES_IV_SIZE];
    unsigned char tag[AES_TAG_SIZE];

    if (fread(iv, 1, sizeof(iv), in) != sizeof(iv)) {
        fclose(in);
        return -1;
    }

    // 파일 맨 끝에 저장된 인증 태그 읽기
    if (fseek(in, -AES_TAG_SIZE, SEEK_END) != 0 ||
        fread(tag, 1, sizeof(tag), in) != sizeof(tag) ||
        fseek(in, AES_IV_SIZE, SEEK_SET) != 0) {
        fclose(in);
        return -1;
    }

    FILE *out = fopen(output_path, "wbx");
    if (!out) {
        fclose(in);
        return -1;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char input[BUFFER_SIZE];
    unsigned char output[BUFFER_SIZE + 16];
    long remaining = file_size - AES_IV_SIZE - AES_TAG_SIZE;
    int len;
    int ok = 0;

    if (!ctx) goto cleanup;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
        goto cleanup;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                            AES_IV_SIZE, NULL) != 1)
        goto cleanup;

    if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1)
        goto cleanup;

    // 암호문을 조금씩 복호화
    while (remaining > 0) {
        size_t chunk = remaining > BUFFER_SIZE
                     ? BUFFER_SIZE : (size_t)remaining;

        if (fread(input, 1, chunk, in) != chunk)
            goto cleanup;

        if (EVP_DecryptUpdate(ctx, output, &len,
                              input, (int)chunk) != 1)
            goto cleanup;

        if (write_all(out, output, (size_t)len) != 0)
            goto cleanup;

        remaining -= (long)chunk;
    }

    // 인증 태그 설정
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                            AES_TAG_SIZE, tag) != 1)
        goto cleanup;

    // 파일 위변조 여부 검증
    if (EVP_DecryptFinal_ex(ctx, output, &len) != 1)
        goto cleanup;

    if (write_all(out, output, (size_t)len) != 0)
        goto cleanup;

    if (fflush(out) != 0 || fsync(fileno(out)) != 0)
        goto cleanup;

    ok = 1;

cleanup:
    EVP_CIPHER_CTX_free(ctx);
    fclose(in);
    if (fclose(out) != 0) ok = 0;

    // 인증 실패 또는 오류가 발생하면 복호화 파일 삭제
    if (!ok) remove(output_path);

    return ok ? 0 : -1;
}
