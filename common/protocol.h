#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef enum {
    CMD_BACKUP = 1,
    CMD_BACKUP_ACK,
    CMD_LIST,
    CMD_LIST_RES,
    CMD_RESTORE,
    CMD_RESTORE_DATA,
    CMD_ERROR,
    CMD_BACKUP_OFFSET
} CommandType;

#pragma pack(push, 1)
typedef struct {
    uint32_t command;
    uint32_t status;
    char filename[256];
    uint64_t file_size;
    uint32_t version;
    char token[65];
    char upload_id[33];
    uint64_t offset;
} BackupHeader;
#pragma pack(pop)

#endif