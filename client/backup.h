#ifndef BACKUP_H
#define BACKUP_H

#include "encryption.h"
#include "integrity.h"

#define BACKUP_DIR "backups"

int backup_file(
    const char *source_path,
    const unsigned char key[AES_KEY_SIZE],
    char output_hash[SHA256_HEX_LENGTH]
);

#endif