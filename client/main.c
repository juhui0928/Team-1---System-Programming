#include <stdio.h>
#include "cli.h"

int main(int argc, char *argv[])
{
    // CLI 명령어 실행
    if (cli_run(argc, argv) != 0) {
        return 1;
    }

    return 0;
}
