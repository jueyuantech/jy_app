#pragma once

#if defined(_MSC_VER)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct dirent {
    char d_name[MAX_PATH];
};

typedef struct DIR {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    struct dirent entry;
    int first;
} DIR;

static inline DIR* opendir(const char* path)
{
    DIR* dir = NULL;
    char search_path[MAX_PATH];

    if (!path) {
        return NULL;
    }

    snprintf(search_path, sizeof(search_path), "%s\\*", path);
    dir = (DIR*)calloc(1, sizeof(*dir));
    if (!dir) {
        return NULL;
    }

    dir->handle = FindFirstFileA(search_path, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }

    dir->first = 1;
    return dir;
}

static inline struct dirent* readdir(DIR* dir)
{
    if (!dir || dir->handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    if (dir->first) {
        dir->first = 0;
    } else if (!FindNextFileA(dir->handle, &dir->data)) {
        return NULL;
    }

    strncpy(dir->entry.d_name, dir->data.cFileName, sizeof(dir->entry.d_name) - 1U);
    dir->entry.d_name[sizeof(dir->entry.d_name) - 1U] = '\0';
    return &dir->entry;
}

static inline int closedir(DIR* dir)
{
    if (!dir) {
        return -1;
    }

    if (dir->handle != INVALID_HANDLE_VALUE) {
        FindClose(dir->handle);
    }
    free(dir);
    return 0;
}

#else
#include_next <dirent.h>
#endif
