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

int load_backup_hash(
    const char *encrypted_path,
    char output_hash[SHA256_HEX_LENGTH]
);

int restore_backup(
    const char *encrypted_path,
    const char *restored_path,
    const unsigned char key[AES_KEY_SIZE]
);

#endif
