#ifndef SHARED_OS_H
#define SHARED_OS_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#endif

#ifdef _WIN32
#define OS_SEP '\\'
#define OS_SEP_STR "\\"
#define OS_PATH_LIST ";"
#define OS_LIB_EXT ".dll"
#define OS_EXE_EXT ".exe"
#define os_stricmp _stricmp
#define os_strnicmp _strnicmp
#define os_zero_memory(p, n) SecureZeroMemory((p), (n))
#else
#include <strings.h>
#define OS_SEP '/'
#define OS_SEP_STR "/"
#define OS_PATH_LIST ":"
#define OS_LIB_EXT ".so"
#define OS_EXE_EXT ""
#define os_stricmp strcasecmp
#define os_strnicmp strncasecmp
#define os_zero_memory(p, n) memset((p), 0, (n))
#endif

#ifdef __cplusplus
extern "C" {
#endif

void os_init(void);
uint32_t os_tick_ms(void);
void os_sleep_ms(unsigned ms);
int os_join(char *out, size_t max, const char *dir, const char *name);
int os_mkdirs(const char *path);
int os_file_exists(const char *path);
int os_dir_exists(const char *path);
uint64_t os_file_size(const char *path);
int os_copy_file(const char *from, const char *to);
int os_delete_file(const char *path);
uint64_t os_file_mtime(const char *path);
void os_data_dir(char *out, size_t max);
int os_exe_dir(char *out, size_t max);
int os_app_root(char *out, size_t max, const char *fallback);
int os_utf8_to_wide(const char *utf8, wchar_t *out, int max);
void os_open_url(const char *url);
int os_launch(const char *path);

typedef int (*os_dir_cb)(const char *name, int is_dir, void *user);
int os_list_dir(const char *path, os_dir_cb cb, void *user);

#ifdef __cplusplus
}
#endif

#endif
