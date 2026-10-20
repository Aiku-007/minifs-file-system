#include "minifs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* Assignment rule: use open/read/write/lseek/close only for file I/O. */

static ssize_t write_all(int fd, const void *buf, size_t count)
{
    const char *ptr = buf;
    size_t total = 0;

    while (total < count) {
        ssize_t written = write(fd, ptr + total, count - total);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (written == 0) {
            return -1;
        }

        total += (size_t)written;
    }

    return (ssize_t)total;
}



static ssize_t read_all(int fd, void *buf, size_t count)
{
    char *ptr = buf;
    size_t total = 0;

    while (total < count) {
        ssize_t bytes_read = read(fd, ptr + total, count - total);

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (bytes_read == 0) {
            break;
        }

        total += (size_t)bytes_read;
    }

    return (ssize_t)total;
}



static int read_metadata(int fd, MiniFSHeader *header,
                         DirectoryEntry entries[])
{
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        return -1;
    }

    ssize_t bytes_read = read_all(fd, header, sizeof(*header));

    if (bytes_read != (ssize_t)sizeof(*header)) {
        fprintf(stderr, "Error: unable to read complete MiniFS header.\n");
        return -1;
    }

    if (memcmp(header->magic, MINIFS_MAGIC,
               strlen(MINIFS_MAGIC)) != 0 ||
        header->num_files < 0 ||
        header->num_files > MAX_FILES) {
        fprintf(stderr, "Error: invalid MiniFS header.\n");
        return -1;
    }

    size_t directory_size = sizeof(DirectoryEntry) * MAX_FILES;

    bytes_read = read_all(fd, entries, directory_size);

    if (bytes_read != (ssize_t)directory_size) {
        fprintf(stderr, "Error: unable to read complete directory.\n");
        return -1;
    }

    return 0;
}



int minifs_create(const char *image)
{
    int fd = open(image, O_CREAT | O_TRUNC | O_RDWR, 0644);

    if (fd == -1) {
        perror("open");
        return -1;
    }

    MiniFSHeader header;
    DirectoryEntry entries[MAX_FILES];

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, MINIFS_MAGIC, strlen(MINIFS_MAGIC));
    header.num_files = 0;

    memset(entries, 0, sizeof(entries));

    if (write_all(fd, &header, sizeof(header)) !=
        (ssize_t)sizeof(header)) {
        perror("write header");
        close(fd);
        return -1;
    }

    if (write_all(fd, entries, sizeof(entries)) !=
        (ssize_t)sizeof(entries)) {
        perror("write directory");
        close(fd);
        return -1;
    }

    if (close(fd) == -1) {
        perror("close");
        return -1;
    }

    printf("MiniFS image created: %s\n", image);
    return 0;
}



int minifs_list(const char *image)
{
    int fd = open(image, O_RDONLY);

    if (fd == -1) {
        perror("open image");
        return -1;
    }

    MiniFSHeader header;
    DirectoryEntry entries[MAX_FILES];

    if (read_metadata(fd, &header, entries) == -1) {
        close(fd);
        return -1;
    }

    if (close(fd) == -1) {
        perror("close image");
        return -1;
    }

    printf("%-32s %10s %10s\n", "Filename", "Size", "Offset");
    printf("----------------------------------------------------------\n");

    for (int i = 0; i < MAX_FILES; i++) {
        if (entries[i].used == 1) {
            printf("%-32.32s %10d %10d\n",
                   entries[i].filename,
                   entries[i].size,
                   entries[i].offset);
        }
    }

    return 0;
}



int minifs_add(const char *image, const char *source)
{
    int src_fd = -1;
    int img_fd = -1;
    int result = -1;

    struct stat src_stat, img_stat;
    MiniFSHeader header;
    DirectoryEntry entries[MAX_FILES];

    /* Get the source filename without its directory path. */
    const char *name = strrchr(source, '/');
    name = (name == NULL) ? source : name + 1;

    size_t name_len = strlen(name);

    if (name_len == 0 || name_len >= MAX_FILENAME) {
        fprintf(stderr, "Error: filename is too long or empty.\n");
        return -1;
    }

    /* Open the source file. */
    src_fd = open(source, O_RDONLY);

    if (src_fd == -1) {
        perror("open source");
        goto cleanup;
    }

    if (fstat(src_fd, &src_stat) == -1) {
        perror("fstat source");
        goto cleanup;
    }

    if (!S_ISREG(src_stat.st_mode)) {
        fprintf(stderr, "Error: source must be a regular file.\n");
        goto cleanup;
    }

    /* Determine source size using lseek(), as required. */
    off_t source_size = lseek(src_fd, 0, SEEK_END);

    if (source_size == (off_t)-1) {
        perror("lseek source size");
        goto cleanup;
    }

    if (source_size < 0 || source_size > INT32_MAX) {
        fprintf(stderr, "Error: source file is too large.\n");
        goto cleanup;
    }

    /* Open the MiniFS image. */
    img_fd = open(image, O_RDWR);

    if (img_fd == -1) {
        perror("open image");
        goto cleanup;
    }

    if (fstat(img_fd, &img_stat) == -1) {
        perror("fstat image");
        goto cleanup;
    }

    /* Prevent adding the image to itself. */
    if (src_stat.st_dev == img_stat.st_dev &&
        src_stat.st_ino == img_stat.st_ino) {
        fprintf(stderr,
                "Error: source and image must be different files.\n");
        goto cleanup;
    }

    /* Read and validate existing metadata. */
    if (read_metadata(img_fd, &header, entries) == -1) {
        goto cleanup;
    }

    /* Find an unused entry and reject duplicate names. */
    int slot = -1;

    for (int i = 0; i < MAX_FILES; i++) {
        if (entries[i].used == 1 &&
            strcmp(entries[i].filename, name) == 0) {
            fprintf(stderr, "Error: file '%s' already exists.\n", name);
            goto cleanup;
        }

        if (slot == -1 && entries[i].used == 0) {
            slot = i;
        }
    }

    if (slot == -1) {
        fprintf(stderr, "Error: MiniFS directory is full.\n");
        goto cleanup;
    }

    /* Append the new file's data to the end of the image. */
    off_t data_offset = lseek(img_fd, 0, SEEK_END);

    if (data_offset == (off_t)-1) {
        perror("lseek image");
        goto cleanup;
    }

    if (data_offset > INT32_MAX ||
        source_size > INT32_MAX - data_offset) {
        fprintf(stderr, "Error: MiniFS image would be too large.\n");
        goto cleanup;
    }

    /* Return to the beginning of the source file. */
    if (lseek(src_fd, 0, SEEK_SET) == (off_t)-1) {
        perror("lseek source");
        goto cleanup;
    }

    /* Copy the source contents into the image. */
    char buffer[4096];
    off_t remaining = source_size;

    while (remaining > 0) {
        size_t chunk = remaining < (off_t)sizeof(buffer)
                     ? (size_t)remaining : sizeof(buffer);

        ssize_t bytes_read = read(src_fd, buffer, chunk);

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("read source");
            goto cleanup;
        }

        if (bytes_read == 0) {
            fprintf(stderr,
                    "Error: source file changed during reading.\n");
            goto cleanup;
        }

        if (write_all(img_fd, buffer, (size_t)bytes_read)
            != bytes_read) {
            perror("write image data");
            goto cleanup;
        }

        remaining -= bytes_read;
    }

    /* Update the directory entry. */
    memset(&entries[slot], 0, sizeof(entries[slot]));
    memcpy(entries[slot].filename, name, name_len);

    entries[slot].size = (int32_t)source_size;
    entries[slot].offset = (int32_t)data_offset;
    entries[slot].used = 1;

    header.num_files++;

    /* Rewrite the header and directory at offset zero. */
    if (lseek(img_fd, 0, SEEK_SET) == (off_t)-1) {
        perror("lseek metadata");
        goto cleanup;
    }

    if (write_all(img_fd, &header, sizeof(header))
        != (ssize_t)sizeof(header)) {
        perror("write header");
        goto cleanup;
    }

    if (write_all(img_fd, entries, sizeof(entries))
        != (ssize_t)sizeof(entries)) {
        perror("write directory");
        goto cleanup;
    }

    printf("Added %s (%d bytes)\n",
           name, entries[slot].size);

    result = 0;

cleanup:
    if (src_fd != -1 && close(src_fd) == -1) {
        perror("close source");
        result = -1;
    }

    if (img_fd != -1 && close(img_fd) == -1) {
        perror("close image");
        result = -1;
    }

    return result;
}



int minifs_extract(const char *image, const char *name,
                   const char *destination)
{
    int img_fd = -1;
    int dest_fd = -1;
    int result = -1;

    struct stat img_stat, dest_stat;
    MiniFSHeader header;
    DirectoryEntry entries[MAX_FILES];

    if (name == NULL || name[0] == '\0' ||
        strlen(name) >= MAX_FILENAME) {
        fprintf(stderr, "Error: invalid stored filename.\n");
        return -1;
    }

    /* Open the MiniFS image for reading. */
    img_fd = open(image, O_RDONLY);

    if (img_fd == -1) {
        perror("open image");
        goto cleanup;
    }

    if (fstat(img_fd, &img_stat) == -1) {
        perror("fstat image");
        goto cleanup;
    }

    /* Read the header and directory. */
    if (read_metadata(img_fd, &header, entries) == -1) {
        goto cleanup;
    }

    /* Find the requested file. */
    int slot = -1;

    for (int i = 0; i < MAX_FILES; i++) {
        if (entries[i].used == 1 &&
            memchr(entries[i].filename, '\0',
                   MAX_FILENAME) != NULL &&
            strcmp(entries[i].filename, name) == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        fprintf(stderr, "Error: stored file '%s' not found.\n", name);
        goto cleanup;
    }

    DirectoryEntry *entry = &entries[slot];

    /* Validate the stored data location and size. */
    off_t metadata_size = (off_t)sizeof(MiniFSHeader) +
                          (off_t)sizeof(DirectoryEntry) * MAX_FILES;

    if (entry->size < 0 ||
        entry->offset < metadata_size ||
        (off_t)entry->offset > img_stat.st_size ||
        (off_t)entry->size > img_stat.st_size - entry->offset) {
        fprintf(stderr, "Error: invalid stored file metadata.\n");
        goto cleanup;
    }

    /* Refuse to overwrite the image itself, including hard links. */
    if (stat(destination, &dest_stat) == 0) {
        if (img_stat.st_dev == dest_stat.st_dev &&
            img_stat.st_ino == dest_stat.st_ino) {
            fprintf(stderr,
                    "Error: destination must differ from the image.\n");
            goto cleanup;
        }
    } else if (errno != ENOENT) {
        perror("stat destination");
        goto cleanup;
    }

    /* Seek directly to the stored file's data. */
    if (lseek(img_fd, (off_t)entry->offset, SEEK_SET) == (off_t)-1) {
        perror("lseek image data");
        goto cleanup;
    }

    /* Create or replace the destination file. */
    dest_fd = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (dest_fd == -1) {
        perror("open destination");
        goto cleanup;
    }

    /* Copy exactly the number of bytes recorded in the directory. */
    char buffer[4096];
    off_t remaining = entry->size;

    while (remaining > 0) {
        size_t chunk = remaining < (off_t)sizeof(buffer)
                     ? (size_t)remaining : sizeof(buffer);

        ssize_t bytes_read = read(img_fd, buffer, chunk);

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("read image data");
            goto cleanup;
        }

        if (bytes_read == 0) {
            fprintf(stderr, "Error: unexpected end of image.\n");
            goto cleanup;
        }

        if (write_all(dest_fd, buffer, (size_t)bytes_read)
            != bytes_read) {
            perror("write destination");
            goto cleanup;
        }

        remaining -= bytes_read;
    }

    if (close(dest_fd) == -1) {
        dest_fd = -1;
        perror("close destination");
        goto cleanup;
    }
    dest_fd = -1;

    printf("Extracted %s to %s\n", name, destination);
    result = 0;

cleanup:
    if (dest_fd != -1 && close(dest_fd) == -1) {
        perror("close destination");
        result = -1;
    }

    if (img_fd != -1 && close(img_fd) == -1) {
        perror("close image");
        result = -1;
    }

    return result;
}


static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s create <image>\n"
        "  %s list <image>\n"
        "  %s add <image> <source-file>\n"
        "  %s extract <image> <stored-name> <destination>\n",
        prog, prog, prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc < 3) { usage(argv[0]); return EXIT_FAILURE; }

    int rc = -1;
    if (strcmp(argv[1], "create") == 0 && argc == 3)
        rc = minifs_create(argv[2]);
    else if (strcmp(argv[1], "list") == 0 && argc == 3)
        rc = minifs_list(argv[2]);
    else if (strcmp(argv[1], "add") == 0 && argc == 4)
        rc = minifs_add(argv[2], argv[3]);
    else if (strcmp(argv[1], "extract") == 0 && argc == 5)
        rc = minifs_extract(argv[2], argv[3], argv[4]);
    else { usage(argv[0]); return EXIT_FAILURE; }

    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
