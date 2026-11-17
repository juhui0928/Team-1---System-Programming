#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <fcntl.h>
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

void handle_backup(int client_sock, BackupHeader *header) {
    char dir_path[512];
    char tmp_path[600];
    char final_path[600];
    char buf[BUF_SIZE];

    snprintf(dir_path, sizeof(dir_path), "backup_storage/%s", header->filename);

    pthread_mutex_lock(&storage_mutex);

    if (mkdir(dir_path, 0700) != 0 && errno != EEXIST) {
        pthread_mutex_unlock(&storage_mutex);
        send_error(client_sock, 2);
        return;
    }

    pthread_mutex_unlock(&storage_mutex);

    // 연결마다 독립적인 임시 파일 사용
    snprintf(tmp_path, sizeof(tmp_path),
             "%s/.upload_XXXXXX", dir_path);

    int temp_fd = mkstemp(tmp_path);

    if (temp_fd < 0) {
        send_error(client_sock, 2);
        return;
    }

    FILE *fp = fdopen(temp_fd, "wb");

    if (!fp) {
        close(temp_fd);
        unlink(tmp_path);
        send_error(client_sock, 2);
        return;
    }

    uint64_t total = 0;
    int recv_ok = 1;
    while (total < header->file_size) {
        uint64_t remain = header->file_size - total;
        size_t chunk = (remain < BUF_SIZE) ? (size_t)remain : BUF_SIZE;
        if (recv_all(client_sock, buf, chunk) < 0) {
            recv_ok = 0;
            break;
        }
        if (fwrite(buf, 1, chunk, fp) != chunk) {
            recv_ok = 0;
            break;
        }
        total += chunk;
    }

    if (!recv_ok) {
        fclose(fp);
        unlink(tmp_path);
        return;
    }

    int saved = fflush(fp) == 0;

    if (saved && fsync(fileno(fp)) != 0)
        saved = 0;

    if (fclose(fp) != 0)
        saved = 0;

    if (!saved) {
        unlink(tmp_path);
        send_error(client_sock, 3);
        return;
    }

    // 전송 완료 후 버전 번호 결정
    pthread_mutex_lock(&storage_mutex);

    uint32_t ver = get_next_version(dir_path);

    snprintf(final_path, sizeof(final_path),
             "%s/version_%03u", dir_path, ver);

    int published = ver != 0 &&
                    link(tmp_path, final_path) == 0;

    unlink(tmp_path);

    int durable = 0;

    if (published) {
        int dir_fd = open(dir_path, O_RDONLY | O_DIRECTORY);

        if (dir_fd >= 0) {
            durable = fsync(dir_fd) == 0;
            close(dir_fd);
        }
    }

    pthread_mutex_unlock(&storage_mutex);

    if (!published || !durable) {
        send_error(client_sock, 4);
        return;
    }

    printf("백업 완료: %s -> version_%03u (%llu bytes)\n",
           header->filename, ver, (unsigned long long)total);

    BackupHeader res;
    memset(&res, 0, sizeof(res));
    res.command = CMD_BACKUP_ACK;
    res.status = 0;
    strncpy(res.filename, header->filename, sizeof(res.filename) - 1);
    res.file_size = total;
    res.version = ver;
    send_all(client_sock, &res, sizeof(res));
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