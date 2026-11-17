#define _POSIX_C_SOURCE 200809L

#include "remote.h"
#include "key.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include "backup.h"
#include "recovery.h"
#include "../common/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>

#define SERVER_PORT 9000
#define BUFFER_SIZE 4096
#define MAX_PACKAGE (1024ULL * 1024ULL * 1024ULL)

// 외부 키 파일에서 AES-256 키 읽기

static int set_auth_token(BackupHeader *header)
{
    const char *token = getenv("CYBERVAULT_TOKEN");

    if (!token || strlen(token) != 64 ||
        strspn(token, "0123456789abcdef") != 64) {
        fprintf(stderr, "[AUTH] 인증 토큰 설정 오류\n");
        return -1;
    }

    memcpy(header->token, token, 65);
    return 0;
}

static int connect_server(void)
{
    const char *ip = getenv("CYBERVAULT_SERVER");
    if (!ip)
        ip = "127.0.0.1";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct timeval timeout = {10, 0};

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
               &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
               &timeout, sizeof(timeout));

    struct sockaddr_in addr = {0};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1 ||
        connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

// 전체 데이터 전송
static int send_bytes(int fd, const void *data, size_t size)
{
    const unsigned char *p = data;

    while (size > 0) {
        ssize_t n = send(fd, p, size, MSG_NOSIGNAL);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;

        p += n;
        size -= (size_t)n;
    }

    return 0;
}

// 전체 데이터 수신
static int recv_bytes(int fd, void *data, size_t size)
{
    unsigned char *p = data;

    while (size > 0) {
        ssize_t n = recv(fd, p, size, 0);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;

        p += n;
        size -= (size_t)n;
    }

    return 0;
}

static int set_filename(BackupHeader *h, const char *name)
{
    if (!name || !*name ||
        strlen(name) >= sizeof(h->filename) ||
        strchr(name, '/') || strchr(name, '\\') ||
        strstr(name, "..") || strcmp(name, ".") == 0)
        return -1;

    strcpy(h->filename, name);
    return 0;
}

// 암호화 파일과 해시를 서버에 전송
int remote_backup(const char *path)
{
    unsigned char key[AES_KEY_SIZE];
    char hash[SHA256_HEX_LENGTH];
    char encrypted[1024];
    unsigned char buf[BUFFER_SIZE];

    int fd = -1;
    FILE *fp = NULL;
    int result = -1;

    BackupHeader h = {0};
    BackupHeader response = {0};

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    if (set_filename(&h, name) != 0 ||
        key_load(key) != 0)
        goto cleanup;

    if (backup_file_ex(path, key, hash,
                       encrypted, sizeof(encrypted)) != 0)
        goto cleanup;

    struct stat st;

    if (stat(encrypted, &st) != 0 ||
        st.st_size < 0 ||
        (uint64_t)st.st_size > MAX_PACKAGE - 64)
        goto cleanup;

    fp = fopen(encrypted, "rb");
    if (!fp)
        goto cleanup;


    unsigned char upload_random[16];

    if (RAND_bytes(upload_random, sizeof(upload_random)) != 1)
        goto cleanup;

    for (size_t i = 0; i < sizeof(upload_random); i++)
        snprintf(h.upload_id + i * 2, 3,
                 "%02x", (unsigned int)upload_random[i]);

    h.command = CMD_BACKUP;
    h.file_size = (uint64_t)st.st_size + 64;

    if (set_auth_token(&h) != 0)
        goto cleanup;

    int test_disconnected = 0;

    // 최초 연결 + 최대 9회 재연결
    for (int attempt = 0; attempt < 10; attempt++) {

        if (attempt > 0) {
            printf("[RESUME] 재연결 시도: %d\n", attempt);
            sleep(2);
        }

        fd = connect_server();

        if (fd < 0)
            continue;

        memset(&response, 0, sizeof(response));

        if (send_bytes(fd, &h, sizeof(h)) != 0 ||
            recv_bytes(fd, &response, sizeof(response)) != 0)
            goto retry;

        if (response.command == CMD_ERROR) {
            if (response.status == 7)
                goto cleanup;
            goto retry;
        }

        // 이미 완료된 업로드의 ACK 재수신
        if (response.command == CMD_BACKUP_ACK &&
            response.status == 0 &&
            response.file_size == h.file_size &&
            response.version != 0)
            goto success;

        if (response.command != CMD_BACKUP_OFFSET ||
            response.status != 0 ||
            response.offset > h.file_size)
            goto retry;

        uint64_t offset = response.offset;

        printf("[RESUME] 서버 저장 위치: %llu / %llu bytes\n",
               (unsigned long long)offset,
               (unsigned long long)h.file_size);

        if (offset < 64) {
            if (send_bytes(fd, hash + offset,
                           64 - (size_t)offset) != 0)
                goto retry;

            offset = 64;
        }

        clearerr(fp);

        if (fseeko(fp, (off_t)(offset - 64), SEEK_SET) != 0)
            goto cleanup;

        while (offset < h.file_size) {
            uint64_t remaining = h.file_size - offset;

            size_t chunk = remaining > sizeof(buf)
                         ? sizeof(buf) : (size_t)remaining;

            size_t n = fread(buf, 1, chunk, fp);

            if (n != chunk)
                goto cleanup;

            if (send_bytes(fd, buf, n) != 0)
                goto retry;

            offset += n;

            // 시연 및 테스트용 강제 연결 단절
            if (!test_disconnected &&
                getenv("CYBERVAULT_TEST_DISCONNECT") &&
                offset >= 65536 &&
                offset < h.file_size) {

                test_disconnected = 1;

                printf("[TEST] 전송 중 강제 연결 단절\n");

                shutdown(fd, SHUT_RDWR);
                goto retry;
            }
        }

        if (recv_bytes(fd, &response, sizeof(response)) != 0 ||
            response.command != CMD_BACKUP_ACK ||
            response.status != 0 ||
            response.file_size != h.file_size ||
            response.version == 0)
            goto retry;

success:
        printf("[REMOTE] 원격 백업 성공: %s\n", name);
        printf("[REMOTE] 서버 버전: %u\n", response.version);

        result = 0;
        goto cleanup;

retry:
        close(fd);
        fd = -1;
    }


cleanup:
    OPENSSL_cleanse(key, sizeof(key));
    if (fp)
        fclose(fp);
    if (fd >= 0)
        close(fd);
    if (result != 0)
        fprintf(stderr, "[REMOTE] 원격 백업 실패\n");

    return result;
}

// 서버 백업 목록 및 버전 조회
int remote_list(const char *filename)
{
    BackupHeader h = {0};
    BackupHeader response = {0};
    char buf[BUFFER_SIZE + 1];

    h.command = CMD_LIST;

    if (filename && set_filename(&h, filename) != 0)
        return -1;

    int fd = connect_server();
    if (fd < 0)
        return -1;

    int result = -1;

    if (set_auth_token(&h) != 0 ||
        send_bytes(fd, &h, sizeof(h)) != 0 ||
        recv_bytes(fd, &response, sizeof(response)) != 0 ||
        response.command != CMD_LIST_RES ||
        response.status != 0 ||
        response.file_size > BUFFER_SIZE)
        goto cleanup;

    if (recv_bytes(fd, buf, (size_t)response.file_size) != 0)
        goto cleanup;

    buf[response.file_size] = '\0';

    printf("[REMOTE] 백업 목록\n%s", buf);
    result = 0;

cleanup:
    close(fd);
    return result;
}

// 서버에서 백업 다운로드 후 안전하게 복구
int remote_restore(const char *filename,
                   const char *output,
                   unsigned int version)
{
    unsigned char key[AES_KEY_SIZE];
    unsigned char buf[BUFFER_SIZE];
    char hash[SHA256_HEX_LENGTH];

    BackupHeader h = {0};
    BackupHeader response = {0};

    int fd = -1;
    FILE *fp = NULL;
    int result = -1;

    char temp_dir[] = "/tmp/cybervault-XXXXXX";
    char encrypted[256];

    if (set_filename(&h, filename) != 0 ||
        key_load(key) != 0) {
        OPENSSL_cleanse(key, sizeof(key));
        return -1;
    }

    if (!mkdtemp(temp_dir)) {
        OPENSSL_cleanse(key, sizeof(key));
        return -1;
    }

    snprintf(encrypted, sizeof(encrypted),
             "%s/payload.enc", temp_dir);

    fd = connect_server();
    if (fd < 0)
        goto cleanup;

    h.command = CMD_RESTORE;
    h.version = version;

    if (set_auth_token(&h) != 0 ||
        send_bytes(fd, &h, sizeof(h)) != 0 ||
        recv_bytes(fd, &response, sizeof(response)) != 0 ||
        response.command != CMD_RESTORE_DATA ||
        response.status != 0 ||
        response.file_size < 64 + AES_IV_SIZE + AES_TAG_SIZE ||
        response.file_size > MAX_PACKAGE)
        goto cleanup;

    // 패키지 앞부분의 SHA-256 읽기
    if (recv_bytes(fd, hash, 64) != 0)
        goto cleanup;

    hash[64] = '\0';

    if (strspn(hash, "0123456789abcdef") != 64)
        goto cleanup;

    fp = fopen(encrypted, "wbx");
    if (!fp)
        goto cleanup;

    uint64_t remaining = response.file_size - 64;

    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buf)
                     ? sizeof(buf) : (size_t)remaining;

        if (recv_bytes(fd, buf, chunk) != 0 ||
            fwrite(buf, 1, chunk, fp) != chunk)
            goto cleanup;

        remaining -= chunk;
    }

    int close_result = fclose(fp);
    fp = NULL;

    if (close_result != 0)
        goto cleanup;

    // 기존 복구 모듈을 그대로 활용
    if (recover_file(encrypted, output, key, hash) != 0)
        goto cleanup;

    printf("[RESTORE] 원격 파일 복구 성공\n");
    printf("[HASH] SHA-256 검증 성공\n");
    printf("[REMOTE] 복구 버전: %u\n", response.version);

    result = 0;

cleanup:
    OPENSSL_cleanse(key, sizeof(key));
    if (fp)
        fclose(fp);
    if (fd >= 0)
        close(fd);

    unlink(encrypted);
    rmdir(temp_dir);

    if (result != 0)
        fprintf(stderr, "[RESTORE] 원격 복구 실패\n");

    return result;
}
