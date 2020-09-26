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
#include <mcpelauncher/linker.h>
#include <libc_shim.h>
#include <stdexcept>
#include <cstring>

void MinecraftUtils::workaroundLocaleBug() {
    setenv("LC_ALL", "C", 1); // HACK: Force set locale to one recognized by MCPE so that the outdated C++ standard library MCPE uses doesn't fail to find one
}

std::unordered_map<std::string, void*> MinecraftUtils::getLibCSymbols() {
    std::unordered_map<std::string, void*> syms;
    for (auto const &s : shim::get_shimmed_symbols())
        syms[s.name] = s.value;
    return syms;
}

void* MinecraftUtils::loadLibM() {
#ifdef __APPLE__
    void* libmLib = HybrisUtils::loadLibraryOS("libm.so", "libm.dylib", libm_symbols, std::unordered_map<std::string, void*>{ {std::string("sincos"), (void*)__sincos }, {std::string("sincosf"), (void*)__sincosf } });
#else
    void* libmLib = HybrisUtils::loadLibraryOS("libm.so", "libm.so.6", libm_symbols);
#endif
    if (libmLib == nullptr)
        throw std::runtime_error("Failed to load libm");
    return libmLib;
}

void* MinecraftUtils::loadFMod() {
    void* fmodLib = HybrisUtils::loadLibraryOS("libfmod.so", PathHelper::findDataFile(std::string("lib/native/") +
#ifdef __APPLE__
#if defined(__i386__)
    // Minecraft releases (linked) with libc++-shared have to use a newer version of libfmod
    // Apple have multi arch libs so this should work.
    (linker::dlopen("libc++_shared.so", 0) ? std::string("x86_64/libfmod.dylib") : (std::string(getLibraryAbi()) + "/libfmod.dylib"))
#else
    getLibraryAbi() + "/libfmod.dylib"
#endif
#else
#ifdef __LP64__
    getLibraryAbi() + "/libfmod.so.12.0"
#else
    // Minecraft releases (linked) with libc++-shared have to use a newer version of libfmod
    getLibraryAbi() + (linker::dlopen("libc++_shared.so", 0) ? "/libfmod.so.12.0" : "/libfmod.so.10.20")
#endif
#endif
), fmod_symbols);
    if (fmodLib == nullptr)
        throw std::runtime_error("Failed to load fmod");
    return fmodLib;
}

void MinecraftUtils::stubFMod() {
    HybrisUtils::stubSymbols("libfmod.so", fmod_symbols, (void*) (void* (*)()) []() {
        Log::warn("Launcher", "FMod stub called");
        return (void*) nullptr;
    });
}

void MinecraftUtils::setupHybris() {
    HybrisUtils::loadLibraryOS("libz.so", 
#ifdef __APPLE__
    "libz.dylib"
#else
    "libz.so.1"
#endif
    , libz_symbols);
    HybrisUtils::hookAndroidLog();
    setupApi();
    linker::load_library("libOpenSLES.so", {});
    linker::load_library("libGLESv1_CM++.so", {});

    linker::load_library("libstdc++.so", {});
    linker::load_library("libz.so", {}); // needed for <0.17
}

void MinecraftUtils::setupApi() {
    std::unordered_map<std::string, void*> syms;
    syms["mcpelauncher_log"] = (void*) Log::log;
    syms["mcpelauncher_vlog"] = (void*) Log::vlog;

    syms["mcpelauncher_hook"] = (void*) (void* (*)(void*, void*, void**)) [](void* sym, void* hook, void** orig) {
        Dl_info i;
        if (!linker::dladdr(sym, &i)) {
            Log::error("Hook", "Failed to resolve hook for symbol %lx", (long unsigned) sym);
            return (void*) nullptr;
        }
        void* handle = linker::dlopen(i.dli_fname, 0);
        std::string tName = HookManager::translateConstructorName(i.dli_sname);
        auto ret = HookManager::instance.createHook(handle, tName.empty() ? i.dli_sname : tName.c_str(), hook, orig);
        linker::dlclose(handle);
        HookManager::instance.applyHooks();
        return (void*) ret;
    };

    syms["mcpelauncher_hook2"] = (void *) (void *(*)(void *, const char *, void *, void **))
            [](void *lib, const char *sym, void *hook, void **orig) {
                return (void *) HookManager::instance.createHook(lib, sym, hook, orig);
            };
    syms["mcpelauncher_hook2_add_library"] = (void *) (void (*)(void*)) [](void* lib) {
        HookManager::instance.addLibrary(lib);
    };
    syms["mcpelauncher_hook2_remove_library"] = (void *) (void (*)(void*)) [](void* lib) {
        HookManager::instance.removeLibrary(lib);
    };
    syms["mcpelauncher_hook2_delete"] = (void *) (void (*)(void*)) [](void* hook) {
        HookManager::instance.deleteHook((HookManager::HookInstance*) hook);
    };
    syms["mcpelauncher_hook2_apply"] = (void *) (void (*)()) []() {
        HookManager::instance.applyHooks();
    };
    linker::load_library("libmcpelauncher_mod.so", syms);
}

void* MinecraftUtils::loadMinecraftLib() {
    void* handle = linker::dlopen("libminecraftpe.so", 0);
    if (handle == nullptr) {
        Log::error("MinecraftUtils", "Failed to load Minecraft: %s", linker::dlerror());
    } else {
        HookManager::instance.addLibrary(handle);
    }
    return handle;
}
const char *MinecraftUtils::getLibraryAbi() {
    return PathHelper::getAbiDir();
}

size_t MinecraftUtils::getLibraryBase(void *handle) {
    return linker::get_library_base(handle);
}

void MinecraftUtils::setupGLES2Symbols(void* (*resolver)(const char *)) {
    int i = 0;
    std::unordered_map<std::string, void*> syms;
    while (true) {
        const char* sym = glesv2_symbols[i];
        if (sym == nullptr)
            break;
        syms[sym] = resolver(sym);
        i++;
    }
    linker::load_library("libGLESv2.so", syms);
}
