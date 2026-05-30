#ifndef DIRENT_MSVC_H
#define DIRENT_MSVC_H

#include <io.h>
#include <string.h>
#include <stdlib.h>

struct dirent {
    char d_name[260]; /* MAX_PATH */
};

typedef struct {
    intptr_t handle;
    struct _finddata_t info;
    struct dirent result;
    int first;
} DIR;

static DIR *opendir(const char *name) {
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) return NULL;
    char path[260];
    strncpy(path, name, 259);
    path[259] = '\0';
    size_t len = strlen(path);
    if (len > 0 && path[len-1] != '/' && path[len-1] != '\\') {
        strcat(path, "/*");
    } else {
        strcat(path, "*");
    }
    dir->handle = _findfirst(path, &dir->info);
    if (dir->handle == -1) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    return dir;
}

static struct dirent *readdir(DIR *dirp) {
    if (!dirp || dirp->handle == -1) return NULL;
    if (dirp->first) {
        dirp->first = 0;
    } else {
        if (_findnext(dirp->handle, &dirp->info) != 0) {
            return NULL;
        }
    }
    
    if (strchr(dirp->info.name, '?') != NULL) {
        extern void set_lha_error_invalid_char(const char* name);
        set_lha_error_invalid_char(dirp->info.name);
        extern void fatal_error(char *fmt, ...);
        fatal_error((char *)"SJIS error");
    }
    
    strncpy(dirp->result.d_name, dirp->info.name, sizeof(dirp->result.d_name));
    dirp->result.d_name[sizeof(dirp->result.d_name)-1] = '\0';
    return &dirp->result;
}

static int closedir(DIR *dirp) {
    if (!dirp) return -1;
    if (dirp->handle != -1) {
        _findclose(dirp->handle);
    }
    free(dirp);
    return 0;
}

#endif /* DIRENT_MSVC_H */
