#include "unpack.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint32_t read_u32(FILE* f) {
    uint32_t val;
    if (fread(&val, 4, 1, f) != 1) return 0;
    return val;
}

static char* read_pkg_string(FILE* f) {
    uint32_t size = read_u32(f);
    char* str = (char*)malloc(size + 1);
    if (!str) return NULL;
    fread(str, 1, size, f);
    str[size] = '\0';
    return str;
}

static void make_path(const char* path) {
    char tmp[1024];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

bool extract_pkg(const char* pkg_path, const char* output_dir) {
    FILE* f = fopen(pkg_path, "rb");
    if (!f) return false;

    char magic[5];
    fread(magic, 1, 4, f);
    magic[4] = '\0';
    if (strcmp(magic, "PKGV") != 0) {
        printf("Unpacker: Invalid magic: %s\n", magic);
        fclose(f);
        return false;
    }

    uint32_t version = read_u32(f);
    printf("Unpacker: Package Version: PKGV%04u\n", version);

    uint32_t file_count = read_u32(f);
    printf("Unpacker: File Count: %u\n", file_count);

    typedef struct {
        char* name;
        uint32_t offset;
        uint32_t size;
    } FileEntry;

    FileEntry* entries = (FileEntry*)malloc(sizeof(FileEntry) * file_count);
    if (!entries) {
        fprintf(stderr, "Unpacker: Failed to allocate memory for %u file entries\n", file_count);
        fclose(f);
        return false;
    }
    for (uint32_t i = 0; i < file_count; i++) {
        entries[i].name = read_pkg_string(f);
        entries[i].offset = read_u32(f);
        entries[i].size = read_u32(f);
    }

    long data_start_pos = ftell(f);

    for (uint32_t i = 0; i < file_count; i++) {
        if (i % 50 == 0 || i == file_count - 1) {
            printf("Unpacker: Extracting file %u/%u: %s\n", i + 1, file_count, entries[i].name);
        }

        char dest_path[1024];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", output_dir, entries[i].name);

        // Create parent directories
        char* last_slash = strrchr(dest_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            make_path(dest_path);
            *last_slash = '/';
        }

        fseek(f, data_start_pos + entries[i].offset, SEEK_SET);
        FILE* out_f = fopen(dest_path, "wb");
        if (!out_f) {
            fprintf(stderr, "Unpacker: Failed to create %s (errno: %d)\n", dest_path, errno);
            free(entries[i].name);
            continue;
        }

        uint8_t buffer[64 * 1024];
        uint32_t remaining = entries[i].size;
        while (remaining > 0) {
            uint32_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
            size_t n = fread(buffer, 1, to_read, f);
            if (n == 0) break;
            fwrite(buffer, 1, n, out_f);
            remaining -= n;
        }
        fclose(out_f);
        free(entries[i].name);
    }

    free(entries);
    fclose(f);
    printf("Unpacker: Extraction completed successfully\n");
    return true;
}
