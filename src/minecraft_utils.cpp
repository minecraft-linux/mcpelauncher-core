#include <mcpelauncher/minecraft_utils.h>
#include <mcpelauncher/patch_utils.h>
#include <mcpelauncher/hybris_utils.h>
#include <mcpelauncher/hook.h>
#include <mcpelauncher/path_helper.h>
#include <mcpelauncher/minecraft_version.h>
#include <minecraft/imported/android_symbols.h>
#include <minecraft/imported/egl_symbols.h>
#include <minecraft/imported/libm_symbols.h>
#include <minecraft/imported/fmod_symbols.h>
#include <minecraft/imported/glesv2_symbols.h>
#include <minecraft/imported/libz_symbols.h>
#include <log.h>
#include <FileUtil.h>
#include <hybris/dlfcn.h>
#include <hybris/hook.h>
#include <stdexcept>
#include <cstring>

extern "C" {
#include <hybris/jb/linker.h>
}

void MinecraftUtils::workaroundLocaleBug() {
    setenv("LC_ALL", "C", 1); // HACK: Force set locale to one recognized by MCPE so that the outdated C++ standard library MCPE uses doesn't fail to find one
}

void MinecraftUtils::setMallocZero() {
    hybris_hook("malloc", (void*) (void* (*)(size_t)) [](size_t n) {
        void* ret = malloc(n);
        memset(ret, 0, n);
        return ret;
    });
}

void* MinecraftUtils::loadLibM() {
#ifdef __APPLE__
    void* libmLib = HybrisUtils::loadLibraryOS("libm.dylib", libm_symbols);
#else
    void* libmLib = HybrisUtils::loadLibraryOS("libm.so.6", libm_symbols);
#endif
    if (libmLib == nullptr)
        throw std::runtime_error("Failed to load libm");
    return libmLib;
}

void* MinecraftUtils::loadFMod() {
#ifdef __APPLE__
    void* fmodLib = HybrisUtils::loadLibraryOS(PathHelper::findDataFile("libs/native/libfmod.dylib"), fmod_symbols);
#else
    void* fmodLib = HybrisUtils::loadLibraryOS(PathHelper::findDataFile("libs/native/libfmod.so.9.16"), fmod_symbols);
#endif
    if (fmodLib == nullptr)
        throw std::runtime_error("Failed to load fmod");
    return fmodLib;
}

void MinecraftUtils::stubFMod() {
    HybrisUtils::stubSymbols(fmod_symbols, (void*) (void* (*)()) []() {
        Log::warn("Launcher", "FMod stub called");
        return (void*) nullptr;
    });
}

void MinecraftUtils::setupHybris() {
#ifndef USE_BIONIC_LIBC
    loadLibM();
#endif
    HybrisUtils::stubSymbols(android_symbols, (void*) (void (*)()) []() {
        Log::warn("Launcher", "Android stub called");
    });
    HybrisUtils::stubSymbols(egl_symbols, (void*) (void (*)()) []() {
        Log::warn("Launcher", "EGL stub called");
    });
    HybrisUtils::loadLibraryOS("libz.so.1", libz_symbols);
    HybrisUtils::hookAndroidLog();
    setupHookApi();
    hybris_hook("mcpelauncher_log", (void*) Log::log);
    hybris_hook("mcpelauncher_vlog", (void*) Log::vlog);
    // load stub libraries
#ifdef USE_BIONIC_LIBC
    if (!load_empty_library("ld-android.so") ||
        !hybris_dlopen(PathHelper::findDataFile("libs/hybris/libc.so").c_str(), 0) ||
        !hybris_dlopen(PathHelper::findDataFile("libs/hybris/libm.so").c_str(), 0))
        throw std::runtime_error("Failed to load Android libc.so/libm.so libraries");
#else
    if (!load_empty_library("libc.so") || !load_empty_library("libm.so"))
        throw std::runtime_error("Failed to load stub libraries");
#endif
    if (!load_empty_library("libandroid.so") || !load_empty_library("liblog.so") || !load_empty_library("libEGL.so") || !load_empty_library("libGLESv2.so") || !load_empty_library("libOpenSLES.so") || !load_empty_library("libfmod.so") || !load_empty_library("libGLESv1_CM.so"))
        throw std::runtime_error("Failed to load stub libraries");
    if (!load_empty_library("libmcpelauncher_mod.so"))
        throw std::runtime_error("Failed to load stub libraries");
    load_empty_library("libstdc++.so");
    load_empty_library("libz.so"); // needed for <0.17
}

void MinecraftUtils::setupHookApi() {
    hybris_hook("mcpelauncher_hook", (void*) (void* (*)(void*, void*, void**)) [](void* sym, void* hook, void** orig) {
        Dl_info i;
        if (!hybris_dladdr(sym, &i)) {
            Log::error("Hook", "Failed to resolve hook for symbol %lx", (long unsigned) sym);
            return (void*) nullptr;
        }
        void* handle = hybris_dlopen(i.dli_fname, 0);
        std::string tName = HookManager::translateConstructorName(i.dli_sname);
        auto ret = HookManager::instance.createHook(handle, tName.empty() ? i.dli_sname : tName.c_str(), hook, orig);
        hybris_dlclose(handle);
        HookManager::instance.applyHooks();
        return (void*) ret;
    });

    hybris_hook("mcpelauncher_hook2", (void *) (void *(*)(void *, const char *, void *, void **))
            [](void *lib, const char *sym, void *hook, void **orig) {
                return (void *) HookManager::instance.createHook(lib, sym, hook, orig);
            });
    hybris_hook("mcpelauncher_hook2_add_library", (void *) (void (*)(void*)) [](void* lib) {
        HookManager::instance.addLibrary(lib);
    });
    hybris_hook("mcpelauncher_hook2_remove_library", (void *) (void (*)(void*)) [](void* lib) {
        HookManager::instance.removeLibrary(lib);
    });
    hybris_hook("mcpelauncher_hook2_delete", (void *) (void (*)(void*)) [](void* hook) {
        HookManager::instance.deleteHook((HookManager::HookInstance*) hook);
    });
    hybris_hook("mcpelauncher_hook2_apply", (void *) (void (*)()) []() {
        HookManager::instance.applyHooks();
    });
}

void* MinecraftUtils::loadMinecraftLib(std::string const& path) {
    // load gnustl_shared.so for <0.15.90.8
    std::string gnustlPath = FileUtil::getParent(path) + "/libgnustl_shared.so";
    if (FileUtil::exists(gnustlPath)) {
        hybris_dlopen(gnustlPath.c_str(), RTLD_LAZY);
    }

    void* handle = hybris_dlopen(path.c_str(), RTLD_LAZY);
    if (handle == nullptr)
        throw std::runtime_error(std::string("Failed to load Minecraft: ") + hybris_dlerror());
    HookManager::instance.addLibrary(handle);
    return handle;
}

void MinecraftUtils::setupForHeadless() {
    setupHybris();
    stubFMod();

    hybris_hook("eglGetProcAddress", (void*) (void (*)()) []() {
        Log::warn("Launcher", "EGL stub called");
    });
}

unsigned int MinecraftUtils::getLibraryBase(void *handle) {
    return ((soinfo*) handle)->base;
}

void MinecraftUtils::setupGLES2Symbols(void* (*resolver)(const char *)) {
    int i = 0;
    while (true) {
        const char* sym = glesv2_symbols[i];
        if (sym == nullptr)
            break;
        hybris_hook(sym, resolver(sym));
        i++;
    }
}

static void workerPoolDestroy(void* th) {
    Log::trace("Launcher", "WorkerPool-related class destroy %lu", (unsigned long) th);
}
void MinecraftUtils::workaroundShutdownCrash(void* handle) {
    // this is an ugly hack to workaround the close app crashes MCPE causes
    unsigned int patchOff = (unsigned int) hybris_dlsym(handle, "_ZN19AppPlatform_androidD2Ev");
    PatchUtils::patchCallInstruction((void*) patchOff, (void*) &workerPoolDestroy, true);
}
