#define _POSIX_C_SOURCE 200809L

#include "key.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

// 안전한 랜덤 암호화 키 생성
int key_generate(const char *path)
{
    unsigned char key[AES_KEY_SIZE];

    if (!path || RAND_bytes(key, sizeof(key)) != 1)
        return -1;

    int fd = open(path,
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
                  0600);

    if (fd < 0) {
        OPENSSL_cleanse(key, sizeof(key));
        perror("키 파일 생성 실패");
        return -1;
    }

    size_t offset = 0;
    int ok = 1;

    while (offset < sizeof(key)) {
        ssize_t n = write(fd, key + offset,
                          sizeof(key) - offset);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            ok = 0;
            break;
        }

        offset += (size_t)n;
    }

    if (fsync(fd) != 0)
        ok = 0;

    if (close(fd) != 0)
        ok = 0;

    OPENSSL_cleanse(key, sizeof(key));

    if (!ok) {
        unlink(path);
        return -1;
    }

    printf("[KEY] 암호화 키 생성 완료: %s\n", path);
    return 0;
}

// 키 파일 보안 검사 및 읽기
int key_load(unsigned char key[AES_KEY_SIZE])
{
    const char *path = getenv("CYBERVAULT_KEY_FILE");

    if (!path) {
        fprintf(stderr, "[KEY] 키 환경변수 미설정\n");
        return -1;
    }

    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);

    if (fd < 0) {
        perror("키 파일 열기 실패");
        return -1;
    }

    struct stat st;

    if (fstat(fd, &st) != 0 ||
        !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid() ||
        (st.st_mode & 0077) != 0 ||
        st.st_size != AES_KEY_SIZE) {

        fprintf(stderr, "[KEY] 안전하지 않은 키 파일\n");
        close(fd);
        return -1;
    }

    size_t offset = 0;

    while (offset < AES_KEY_SIZE) {
        ssize_t n = read(fd, key + offset,
                         AES_KEY_SIZE - offset);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            close(fd);
            OPENSSL_cleanse(key, AES_KEY_SIZE);
            return -1;
        }

        offset += (size_t)n;
    }

    close(fd);
    return 0;
}
