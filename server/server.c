#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "protocol.h"

#define PORT 9000
#define BUF_SIZE 4096
#define MAX_FILE_SIZE (1024ULL * 1024ULL * 1024ULL)

static pthread_mutex_t storage_mutex = PTHREAD_MUTEX_INITIALIZER;

int send_all(int sock, const void *buf, size_t len) {
    const char *ptr = (const char *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(sock, ptr + total, len - total, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

int recv_all(int sock, void *buf, size_t len) {
    char *ptr = (char *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(sock, ptr + total, len - total, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

int is_valid_filename(const char *name) {
    size_t len = strnlen(name, 256);
    if (len == 0 || len >= 256) return 0;
    if (strcmp(name, ".") == 0) return 0;
    if (strstr(name, "..") != NULL) return 0;
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) return 0;
    return 1;
}


static int authenticate(const BackupHeader *header)
{
    const char *expected = getenv("CYBERVAULT_TOKEN");

    if (!expected || strlen(expected) != 64 ||
        header->token[64] != '\0')
        return 0;

    unsigned int diff = 0;

    for (size_t i = 0; i < 64; i++)
        diff |= (unsigned char)expected[i] ^
                (unsigned char)header->token[i];

    return diff == 0;
}

int validate_header(const BackupHeader *header) {
    if (header->command < CMD_BACKUP || header->command > CMD_ERROR) {
        return 0;
    }
    if (header->command == CMD_BACKUP || header->command == CMD_RESTORE) {
        if (!is_valid_filename(header->filename)) return 0;
    }
    if (header->command == CMD_LIST && header->filename[0] != '\0') {
        if (!is_valid_filename(header->filename)) return 0;
    }
    if (header->command == CMD_BACKUP && header->file_size > MAX_FILE_SIZE) {
        return 0;
    }
    return 1;
}

void send_error(int sock, uint32_t err_code) {
    BackupHeader res;
    memset(&res, 0, sizeof(res));
    res.command = CMD_ERROR;
    res.status = err_code;
    send_all(sock, &res, sizeof(res));
}

uint32_t get_next_version(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 1;

    uint32_t max_ver = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "version_", 8) == 0 && strstr(entry->d_name, ".tmp") == NULL) {
            uint32_t v = (uint32_t)atoi(entry->d_name + 8);
            if (v > max_ver) max_ver = v;
        }
    }
    closedir(dir);
    return max_ver + 1;
}

uint32_t get_latest_version(const char *dir_path) {
    uint32_t next_v = get_next_version(dir_path);
    return (next_v > 1) ? (next_v - 1) : 0;
}


// 완료된 업로드의 버전 번호 확인
static uint32_t find_completed_version(
    const char *dir_path, const struct stat *uploaded)
{
    DIR *dir = opendir(dir_path);

    if (!dir)
        return 0;

    struct dirent *entry;
    uint32_t version = 0;

    while ((entry = readdir(dir)) != NULL) {

        if (strncmp(entry->d_name, "version_", 8) != 0)
            continue;

        char *end;
        unsigned long v = strtoul(entry->d_name + 8,
                                  &end, 10);

        if (*end || v == 0 || v > UINT32_MAX)
            continue;

        char path[1024];
        struct stat st;

        snprintf(path, sizeof(path), "%s/%s",
                 dir_path, entry->d_name);

        if (stat(path, &st) == 0 &&
            st.st_dev == uploaded->st_dev &&
            st.st_ino == uploaded->st_ino) {

            version = (uint32_t)v;
            break;
        }
    }

    closedir(dir);
    return version;
}

void handle_backup(int client_sock, BackupHeader *header)
{
    char dir_path[512];
    char tmp_path[600];
    char final_path[600];
    char buf[BUF_SIZE];

    int fd = -1;
    uint32_t ver = 0;

    BackupHeader res = {0};

    if (header->upload_id[32] != '\0' ||
        strspn(header->upload_id,
               "0123456789abcdef") != 32 ||
        header->file_size < 92 ||
        header->file_size > MAX_FILE_SIZE) {

        send_error(client_sock, 1);
        return;
    }

    snprintf(dir_path, sizeof(dir_path),
             "backup_storage/%s", header->filename);

    if (mkdir(dir_path, 0700) != 0 &&
        errno != EEXIST) {

        send_error(client_sock, 2);
        return;
    }

    // 업로드 ID와 전체 크기를 기준으로 체크포인트 유지
    snprintf(tmp_path, sizeof(tmp_path),
             "%s/.upload_%s_%llu",
             dir_path, header->upload_id,
             (unsigned long long)header->file_size);

    fd = open(tmp_path,
              O_RDWR | O_CREAT | O_NOFOLLOW,
              0600);

    if (fd < 0) {
        send_error(client_sock, 2);
        return;
    }

    // 같은 업로드의 중복 동시 처리 방지
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        send_error(client_sock, 8);
        close(fd);
        return;
    }

    struct stat st;

    if (fstat(fd, &st) != 0 ||
        !S_ISREG(st.st_mode) ||
        st.st_size < 0 ||
        (uint64_t)st.st_size > header->file_size) {

        send_error(client_sock, 3);
        goto cleanup;
    }

    uint64_t total = (uint64_t)st.st_size;

    // ACK 유실 후 재접속해도 새 버전을 만들지 않음
    pthread_mutex_lock(&storage_mutex);
    ver = find_completed_version(dir_path, &st);
    pthread_mutex_unlock(&storage_mutex);

    if (ver != 0) {
        if (total != header->file_size) {
            send_error(client_sock, 3);
            goto cleanup;
        }

        goto acknowledge;
    }

    res.command = CMD_BACKUP_OFFSET;
    res.offset = total;
    res.file_size = header->file_size;

    if (send_all(client_sock, &res, sizeof(res)) != 0)
        goto cleanup;

    if (lseek(fd, (off_t)total, SEEK_SET) < 0)
        goto cleanup;

    while (total < header->file_size) {

        uint64_t remaining = header->file_size - total;

        size_t chunk = remaining > sizeof(buf)
                     ? sizeof(buf) : (size_t)remaining;

        ssize_t n = recv(client_sock, buf, chunk, 0);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            fsync(fd);

            printf("[RESUME] 체크포인트 저장: %llu bytes\n",
                   (unsigned long long)total);

            goto cleanup;
        }

        size_t written = 0;

        while (written < (size_t)n) {
            ssize_t w = write(fd, buf + written,
                              (size_t)n - written);

            if (w < 0 && errno == EINTR)
                continue;

            if (w <= 0) {
                send_error(client_sock, 3);
                goto cleanup;
            }

            written += (size_t)w;
            total += (uint64_t)w;
        }
    }

    if (fsync(fd) != 0) {
        send_error(client_sock, 3);
        goto cleanup;
    }

    // 완전한 파일만 정상 버전으로 등록
    pthread_mutex_lock(&storage_mutex);

    ver = get_next_version(dir_path);

    snprintf(final_path, sizeof(final_path),
             "%s/version_%03u", dir_path, ver);

    int published = ver != 0 &&
                    link(tmp_path, final_path) == 0;

    pthread_mutex_unlock(&storage_mutex);

    if (!published) {
        send_error(client_sock, 4);
        goto cleanup;
    }

acknowledge:
    {
        int dir_fd = open(dir_path, O_RDONLY | O_DIRECTORY);

        if (dir_fd < 0) {
            send_error(client_sock, 4);
            goto cleanup;
        }

        int durable = fsync(dir_fd);
        close(dir_fd);

        if (durable != 0) {
            send_error(client_sock, 4);
            goto cleanup;
        }
    }

    memset(&res, 0, sizeof(res));

    res.command = CMD_BACKUP_ACK;
    res.file_size = header->file_size;
    res.version = ver;

    strncpy(res.filename, header->filename,
            sizeof(res.filename) - 1);

    send_all(client_sock, &res, sizeof(res));

    printf("[BACKUP] 완료: %s / version_%03u\n",
           header->filename, ver);

cleanup:
    close(fd);
}

void handle_list(int client_sock, BackupHeader *header) {
    char target_dir[512];
    if (header->filename[0] != '\0') {
        snprintf(target_dir, sizeof(target_dir), "backup_storage/%s", header->filename);
    } else {
        snprintf(target_dir, sizeof(target_dir), "backup_storage");
    }

    DIR *dir = opendir(target_dir);
    if (!dir) {
        send_error(client_sock, 5);
        return;
    }

    char list_data[BUF_SIZE] = {0};
    size_t offset = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strstr(entry->d_name, ".tmp") != NULL) continue;
        int written = snprintf(list_data + offset, sizeof(list_data) - offset, "%s\n", entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(list_data) - offset) break;
        offset += (size_t)written;
    }
    closedir(dir);

    BackupHeader res;
    memset(&res, 0, sizeof(res));
    res.command = CMD_LIST_RES;
    res.status = 0;
    strncpy(res.filename, header->filename, sizeof(res.filename) - 1);
    res.file_size = offset;
    send_all(client_sock, &res, sizeof(res));

    if (offset > 0) {
        send_all(client_sock, list_data, offset);
    }
}

void handle_restore(int client_sock, BackupHeader *header) {
    char dir_path[512];
    char filepath[600];
    snprintf(dir_path, sizeof(dir_path), "backup_storage/%s", header->filename);

    uint32_t target_ver = header->version;
    if (target_ver == 0) {
        target_ver = get_latest_version(dir_path);
    }
    if (target_ver == 0) {
        send_error(client_sock, 6);
        return;
    }

    snprintf(filepath, sizeof(filepath), "%s/version_%03u", dir_path, target_ver);
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        send_error(client_sock, 6);
        return;
    }

    fseek(fp, 0, SEEK_END);
    uint64_t fsize = (uint64_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    BackupHeader res;
    memset(&res, 0, sizeof(res));
    res.command = CMD_RESTORE_DATA;
    res.status = 0;
    strncpy(res.filename, header->filename, sizeof(res.filename) - 1);
    res.file_size = fsize;
    res.version = target_ver;

    if (send_all(client_sock, &res, sizeof(res)) < 0) {
        fclose(fp);
        return;
    }

    char buf[BUF_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (send_all(client_sock, buf, n) < 0) break;
    }
    fclose(fp);
    printf("복구 전송 완료: %s (version_%03u)\n", header->filename, target_ver);
}

void *client_worker(void *arg) {
    int client_sock = *(int *)arg;
    free(arg);

    struct timeval timeout = {30, 0};

    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO,
               &timeout, sizeof(timeout));
    setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO,
               &timeout, sizeof(timeout));

    BackupHeader header;
    if (recv_all(client_sock, &header, sizeof(header)) < 0) {
        close(client_sock);
        return NULL;
    }

    if (!authenticate(&header)) {
        fprintf(stderr, "[AUTH] 인증 실패\\n");
        send_error(client_sock, 7);
        close(client_sock);
        return NULL;
    }

    if (!validate_header(&header)) {
        printf("헤더 검증 실패: command=%u, filename=%.255s\n", header.command, header.filename);
        send_error(client_sock, 1);
        close(client_sock);
        return NULL;
    }

    if (header.command == CMD_BACKUP) {
        handle_backup(client_sock, &header);
    } else if (header.command == CMD_LIST) {
        handle_list(client_sock, &header);
    } else if (header.command == CMD_RESTORE) {
        handle_restore(client_sock, &header);
    } else {
        send_error(client_sock, 1);
    }

    close(client_sock);
    return NULL;
}

int main(void) {
    const char *token = getenv("CYBERVAULT_TOKEN");

    if (!token || strlen(token) != 64 ||
        strspn(token, "0123456789abcdef") != 64) {
        fprintf(stderr, "[AUTH] 서버 인증 토큰 설정 필요\n");
        return 1;
    }

    mkdir("backup_storage", 0755);

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, 16) < 0) {
        perror("listen");
        close(server_sock);
        return 1;
    }

    printf("CyberVault 멀티스레드 서버 대기 중 (포트 %d)...\n", PORT);

    while (1) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) continue;

        int *pclient = malloc(sizeof(int));
        if (!pclient) {
            close(client_sock);
            continue;
        }
        *pclient = client_sock;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_worker, pclient) != 0) {
            free(pclient);
            close(client_sock);
            continue;
        }
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}