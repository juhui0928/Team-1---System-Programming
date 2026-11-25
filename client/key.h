#ifndef KEY_H
#define KEY_H

#include "encryption.h"

int key_generate(const char *path);
int key_load(unsigned char key[AES_KEY_SIZE]);

#endif
