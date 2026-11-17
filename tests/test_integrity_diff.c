#include <stdio.h>
#include "../client/integrity.h"

int main(void)
{
    int result = verify_file_integrity("test1.txt", "test2.txt");

    if (result == 0) {
        printf("PASS: 서로 다른 파일을 정상적으로 구별했습니다.\n");
        return 0;
    }

    printf("FAIL: 무결성 검사 결과가 잘못되었습니다.\n");
    return 1;
}
