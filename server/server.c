#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "protocol.h"

#define PORT 9000
#define BUF_SIZE 4096

int main(void) {
    mkdir("backup_storage", 0755);

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_sock, 5);
    printf("서버 대기 중 (포트 %d)...\n", PORT);

    while (1) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) continue;

        BackupHeader header;
        if (recv(client_sock, &header, sizeof(header), MSG_WAITALL) <= 0) {
            close(client_sock);
            continue;
        }

        char filepath[512];
        char buf[BUF_SIZE];

        if (header.command == CMD_BACKUP) {
            snprintf(filepath, sizeof(filepath), "backup_storage/%s", header.filename);
            FILE *fp = fopen(filepath, "wb");

            uint64_t total = 0;
            while (total < header.file_size) {
                int n = recv(client_sock, buf, BUF_SIZE, 0);
                if (n <= 0) break;
                fwrite(buf, 1, n, fp);
                total += n;
            }
            fclose(fp);
            printf("백업 완료: %s (%lu bytes)\n", header.filename, total);

            header.command = CMD_BACKUP_ACK;
            header.status = 0;
            send(client_sock, &header, sizeof(header), 0);
        }
        else if (header.command == CMD_LIST) {
            FILE *pp = popen("ls -1 backup_storage", "r");
            char list_data[BUF_SIZE] = {0};
            size_t len = fread(list_data, 1, sizeof(list_data) - 1, pp);
            pclose(pp);

            header.command = CMD_LIST_RES;
            header.file_size = len;
            send(client_sock, &header, sizeof(header), 0);
            if (len > 0) send(client_sock, list_data, len, 0);
        }
        else if (header.command == CMD_RESTORE) {
            snprintf(filepath, sizeof(filepath), "backup_storage/%s", header.filename);
            FILE *fp = fopen(filepath, "rb");

            if (!fp) {
                header.command = CMD_ERROR;
                header.status = 1;
                send(client_sock, &header, sizeof(header), 0);
            } else {
                fseek(fp, 0, SEEK_END);
                header.file_size = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                header.command = CMD_RESTORE_DATA;
                header.status = 0;
                send(client_sock, &header, sizeof(header), 0);

                size_t n;
                while ((n = fread(buf, 1, BUF_SIZE, fp)) > 0) {
                    send(client_sock, buf, n, 0);
                }
                fclose(fp);
                printf("복구 전송 완료: %s\n", header.filename);
            }
        }

        close(client_sock);
    }

    close(server_sock);
    return 0;
}