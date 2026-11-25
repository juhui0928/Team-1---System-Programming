#ifndef REMOTE_H
#define REMOTE_H

int remote_backup(const char *path);
int remote_list(const char *filename);
int remote_restore(const char *filename,
                   const char *output,
                   unsigned int version);

#endif
