#include "backup.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

int backup_file(
    const char *source_path,
    const unsigned char key[AES_KEY_SIZE],
    char output_hash[SHA256_HEX_LENGTH])
{
    if (source_path == NULL || key == NULL ||
        output_hash == NULL)
        return -1;

    // 백업 디렉터리 생성
    if (mkdir(BACKUP_DIR, 0700) != 0 && errno != EEXIST) {
        perror("백업 디렉터리 생성 실패");
        return -1;
    }

    // 원본 파일명 추출
    const char *filename = strrchr(source_path, '/');
    filename = filename ? filename + 1 : source_path;

    if (*filename == '\0')
        return -1;

    // 암호화 파일 경로 생성
    char encrypted_path[512];

    int len = snprintf(encrypted_path, sizeof(encrypted_path),
                       "%s/%s.enc", BACKUP_DIR, filename);

    if (len < 0 || (size_t)len >= sizeof(encrypted_path))
        return -1;

    // 원본 파일 해시 계산
    if (calculate_sha256(source_path, output_hash) != 0) {
        fprintf(stderr, "SHA-256 계산 실패\n");
        return -1;
    }

    // 암호화된 백업 파일 생성
    if (encrypt_file(source_path, encrypted_path, key) != 0) {
        fprintf(stderr, "파일 암호화 실패: %s\n", source_path);
        return -1;
    }

    printf("[BACKUP] 백업 완료: %s\n", encrypted_path);
    printf("[BACKUP] SHA-256: %s\n", output_hash);

    return 0;
}