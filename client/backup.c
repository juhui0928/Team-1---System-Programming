#define _POSIX_C_SOURCE 200809L

#include "backup.h"
#include "recovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

// SHA-256 문자열 유효성 검사
static int valid_hash(const char *hash)
{
    return strlen(hash) == SHA256_HEX_LENGTH - 1 &&
           strspn(hash, "0123456789abcdefABCDEF")
           == SHA256_HEX_LENGTH - 1;
}

// 암호화 백업 및 버전 관리
int backup_file_ex(
    const char *source_path,
    const unsigned char key[AES_KEY_SIZE],
    char output_hash[SHA256_HEX_LENGTH],
    char *output_path,
    size_t output_path_size)
{
    if (!source_path || !key || !output_hash ||
        !output_path || output_path_size == 0)
        return -1;

    output_path[0] = '\0';

    struct stat st;

    if (stat(source_path, &st) != 0 ||
        !S_ISREG(st.st_mode))
        return -1;

    if (mkdir(BACKUP_DIR, 0700) != 0 &&
        errno != EEXIST)
        return -1;

    const char *filename = strrchr(source_path, '/');
    filename = filename ? filename + 1 : source_path;

    if (!*filename)
        return -1;

    char timestamp[32];
    time_t now = time(NULL);
    struct tm tm_info;

    if (!localtime_r(&now, &tm_info))
        return -1;

    if (!strftime(timestamp, sizeof(timestamp),
                  "%Y%m%d_%H%M%S", &tm_info))
        return -1;

    // 고유한 백업 버전 디렉터리 생성
    char version_dir[512];

    int n = snprintf(version_dir, sizeof(version_dir),
                     "%s/%s_XXXXXX",
                     BACKUP_DIR, timestamp);

    if (n < 0 || (size_t)n >= sizeof(version_dir))
        return -1;

    if (!mkdtemp(version_dir))
        return -1;

    char encrypted_path[1024];
    char hash_path[1100];
    char verify_path[1024];
    char verified_hash[SHA256_HEX_LENGTH];

    int result = -1;

    snprintf(encrypted_path, sizeof(encrypted_path),
             "%s/%s.enc", version_dir, filename);

    snprintf(hash_path, sizeof(hash_path),
             "%s.sha256", encrypted_path);

    snprintf(verify_path, sizeof(verify_path),
             "%s/.verify", version_dir);

    // 원본 SHA-256 계산
    if (calculate_sha256(source_path, output_hash) != 0)
        goto cleanup;

    // AES-256-GCM 암호화
    if (encrypt_file(source_path, encrypted_path, key) != 0)
        goto cleanup;

    // 암호화된 데이터가 원본 해시와 일치하는지 확인
    if (decrypt_file(encrypted_path, verify_path, key) != 0)
        goto cleanup;

    if (calculate_sha256(verify_path, verified_hash) != 0)
        goto cleanup;

    if (strcmp(output_hash, verified_hash) != 0)
        goto cleanup;

    unlink(verify_path);

    // SHA-256 해시 영구 저장
    FILE *fp = fopen(hash_path, "w");

    if (!fp)
        goto cleanup;

    int failed = fprintf(fp, "%s\n", output_hash) < 0;

    if (fflush(fp) != 0)
        failed = 1;

    if (fsync(fileno(fp)) != 0)
        failed = 1;

    if (fclose(fp) != 0)
        failed = 1;

    if (failed)
        goto cleanup;

    if (strlen(encrypted_path) >= output_path_size)
        goto cleanup;

    memcpy(output_path, encrypted_path,
           strlen(encrypted_path) + 1);

    printf("[BACKUP] 백업 완료: %s\n", encrypted_path);
    printf("[BACKUP] SHA-256: %s\n", output_hash);

    result = 0;

cleanup:
    unlink(verify_path);

    if (result != 0) {
        unlink(encrypted_path);
        unlink(hash_path);
        rmdir(version_dir);
        fprintf(stderr, "[BACKUP] 백업 실패\n");
    }

    return result;
}

// 저장된 SHA-256 해시 불러오기
int load_backup_hash(
    const char *encrypted_path,
    char output_hash[SHA256_HEX_LENGTH])
{
    if (!encrypted_path || !output_hash)
        return -1;

    char hash_path[1100];

    int n = snprintf(hash_path, sizeof(hash_path),
                     "%s.sha256", encrypted_path);

    if (n < 0 || (size_t)n >= sizeof(hash_path))
        return -1;

    FILE *fp = fopen(hash_path, "r");

    if (!fp)
        return -1;

    char buffer[128];

    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return -1;
    }

    fclose(fp);

    buffer[strcspn(buffer, "\r\n")] = '\0';

    if (!valid_hash(buffer))
        return -1;

    memcpy(output_hash, buffer, SHA256_HEX_LENGTH);

    return 0;
}

// 저장된 해시를 이용한 안전한 복구
int restore_backup(
    const char *encrypted_path,
    const char *restored_path,
    const unsigned char key[AES_KEY_SIZE])
{
    char expected_hash[SHA256_HEX_LENGTH];

    if (load_backup_hash(encrypted_path, expected_hash) != 0) {
        fprintf(stderr, "[RESTORE] 해시 불러오기 실패\n");
        return -1;
    }

    return recover_file(
        encrypted_path,
        restored_path,
        key,
        expected_hash
    );
}


// 기존 백업 함수와의 호환성 유지
int backup_file(
    const char *source_path,
    const unsigned char key[AES_KEY_SIZE],
    char output_hash[SHA256_HEX_LENGTH])
{
    char path[1024];

    return backup_file_ex(
        source_path, key, output_hash,
        path, sizeof(path));
}
