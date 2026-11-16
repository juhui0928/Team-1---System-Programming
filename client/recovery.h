
#ifndef RECOVERY_H
#define RECOVERY_H

#include "encryption.h"
#include "integrity.h"

// 암호화된 백업을 복호화하고 SHA-256으로 검증하여 복구
// 성공: 0, 실패: -1
// 복구 대상 파일이 이미 존재하면 덮어쓰지 않고 실패
int recover_file(
    const char *encrypted_path,
    const char *restored_path,
    const unsigned char key[AES_KEY_SIZE],
    const char expected_hash[SHA256_HEX_LENGTH]
);

#endif
