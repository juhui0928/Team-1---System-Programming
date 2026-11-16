
#ifndef INTEGRITY_H
#define INTEGRITY_H

#define SHA256_HEX_LENGTH 65

// 파일의 SHA-256 해시값을 계산한다.
// 성공하면 0, 실패하면 -1을 반환한다.
int calculate_sha256(const char *filepath,
                     char output[SHA256_HEX_LENGTH]);

// 두 파일의 SHA-256 해시값을 비교한다.
// 같으면 1, 다르면 0, 오류가 발생하면 -1을 반환한다.
int verify_file_integrity(const char *file1,
                          const char *file2);

#endif
