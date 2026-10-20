#ifndef MINIFS_H
#define MINIFS_H

#include <stdint.h>

#define MINIFS_MAGIC "MINIFS1"
#define MAX_FILES 16
#define MAX_FILENAME 32

typedef struct {
    char magic[8];
    int32_t num_files;
} MiniFSHeader;

typedef struct {
    char filename[MAX_FILENAME];
    int32_t size;
    int32_t offset;
    int32_t used;
} DirectoryEntry;

int minifs_create(const char *image);
int minifs_list(const char *image);
int minifs_add(const char *image, const char *source);
int minifs_extract(const char *image, const char *name, const char *destination);

#endif
