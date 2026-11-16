
#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <stddef.h>

#define AES_KEY_SIZE 32
#define AES_IV_SIZE 12
#define AES_TAG_SIZE 16

// 파일 암호화
// 성공: 0, 실패: -1
int encrypt_file(const char *input_path,
                 const char *output_path,
                 const unsigned char key[AES_KEY_SIZE]);

// 파일 복호화
// 인증 태그 검증까지 성공하면 0, 실패하면 -1
int decrypt_file(const char *input_path,
                 const char *output_path,
                 const unsigned char key[AES_KEY_SIZE]);

#endif
