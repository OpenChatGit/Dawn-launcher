#include "hot_reload.h"
#include "config.h"
#include "shared/os.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE OsModule;
#else
#include <dlfcn.h>
typedef void *OsModule;
#endif

static OsModule g_module;
static const AppApi *g_api;
static uint32_t g_generation;
static char g_error[512];
static char g_loaded_path[1024];

static void
set_error(const char *text)
{
    snprintf(g_error, sizeof(g_error), "%s", text ? text : "");
}

static OsModule
os_load_lib(const char *path)
{
#ifdef _WIN32
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW);
#endif
}

static void
os_unload_lib(OsModule module)
{
    if (!module) {
        return;
    }
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
}

static app_get_api_fn
os_lib_sym(OsModule module)
{
#ifdef _WIN32
    FARPROC symbol = GetProcAddress(module, "app_get_api");
    app_get_api_fn getter;
    memcpy(&getter, &symbol, sizeof(getter));
    return getter;
#else
    union {
        void *ptr;
        app_get_api_fn fn;
    } u;
    u.ptr = dlsym(module, "app_get_api");
    return u.fn;
#endif
}

int
hot_reload_init(AppMemory *memory)
{
    memset(memory, 0, sizeof(*memory));
    memory->api_version = APP_API_VERSION;
    g_generation = 0;
    set_error("");
    return hot_reload_swap();
}

void
hot_reload_shutdown(void)
{
    if (g_module) {
        os_unload_lib(g_module);
        g_module = NULL;
    }
    g_api = NULL;
}

/*
 * Shipped layout: app_logic sits next to the executable. The dev tree keeps
 * it in the CMake build dir (which is also where the hot-reload builder
 * writes). Try the executable folder first so a packaged launcher never
 * depends on the path the CI machine happened to build in.
 */
static int
find_logic_lib(char *out, size_t max)
{
    char dir[1024];

    if (os_exe_dir(dir, sizeof(dir)) &&
        os_join(out, max, dir, "app_logic" OS_LIB_EXT) &&
        os_file_exists(out)) {
        return 1;
    }
    if (os_join(out, max, APP_BUILD_DIR, "app_logic" OS_LIB_EXT) && os_file_exists(out)) {
        return 1;
    }
    return 0;
}

/* Hot copies go next to the source when that folder is writable, else to the user data dir. */
static int
hot_copy_dir(const char *source, char *out, size_t max)
{
    char dir[1024];
    char probe[1024];
    FILE *file;

    snprintf(dir, sizeof(dir), "%s", source);
    {
        size_t n = strlen(dir);
        while (n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\') {
            dir[--n] = '\0';
        }
        if (n > 1) {
            dir[n - 1] = '\0';
        }
    }
    if (dir[0] && os_join(out, max, dir, "hot") && os_mkdirs(out) &&
        os_join(probe, sizeof(probe), out, ".w")) {
        file = fopen(probe, "wb");
        if (file) {
            fclose(file);
            os_delete_file(probe);
            return 1;
        }
    }
    os_data_dir(dir, sizeof(dir));
    return os_join(out, max, dir, "hot") && os_mkdirs(out);
}

int
hot_reload_swap(void)
{
    char source[1024];
    char dest[1024];
    char hot_dir[1024];

    if (!find_logic_lib(source, sizeof(source))) {
        set_error("app_logic" OS_LIB_EXT " not found next to the launcher");
        return 0;
    }
    if (!hot_copy_dir(source, hot_dir, sizeof(hot_dir))) {
        set_error("no writable hot-reload folder");
        return 0;
    }

    g_generation += 1;
    char name[64];
    snprintf(name, sizeof(name), "app_logic_%u" OS_LIB_EXT, g_generation);
    if (!os_join(dest, sizeof(dest), hot_dir, name)) {
        set_error("hot path too long");
        g_generation -= 1;
        return 0;
    }

    OsModule next = NULL;
    if (os_copy_file(source, dest)) {
        next = os_load_lib(dest);
    }
    if (!next) {
        /* Fall back to loading in place (no hot reload, but the app runs). */
        snprintf(dest, sizeof(dest), "%s", source);
        next = os_load_lib(dest);
    }
    if (!next) {
#ifdef _WIN32
        set_error("could not load app_logic");
#else
        {
            const char *err = dlerror();
            snprintf(g_error, sizeof(g_error), "could not load app_logic: %s", err ? err : "unknown");
        }
#endif
        g_generation -= 1;
        return 0;
    }

    app_get_api_fn getter = os_lib_sym(next);
    if (!getter) {
        os_unload_lib(next);
        set_error("app_get_api export missing");
        g_generation -= 1;
        return 0;
    }

    const AppApi *api = getter();
    if (!api || api->version != APP_API_VERSION) {
        os_unload_lib(next);
        set_error("app api version mismatch");
        g_generation -= 1;
        return 0;
    }

    OsModule previous = g_module;
    g_module = next;
    g_api = api;
    snprintf(g_loaded_path, sizeof(g_loaded_path), "%s", dest);
    set_error("");

    if (previous) {
        os_unload_lib(previous);
        if (g_generation > 1) {
            char old_name[64];
            char old_path[1024];
            snprintf(old_name, sizeof(old_name), "app_logic_%u" OS_LIB_EXT, g_generation - 1);
            if (os_join(old_path, sizeof(old_path), hot_dir, old_name)) {
                os_delete_file(old_path);
            }
        }
    }

    return 1;
}

const AppApi *
hot_reload_api(void)
{
    return g_api;
}

const char *
hot_reload_error(void)
{
    return g_error;
}
