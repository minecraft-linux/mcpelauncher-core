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

void* MinecraftUtils::loadLibC() {
    std::unordered_map<std::string, void*> syms;
    for (auto const &s : shim::get_shimmed_symbols())
        syms[s.name] = s.value;
    return linker::load_library("libc.so", syms);
}

void* MinecraftUtils::loadLibM() {
#ifdef __APPLE__
    void* libmLib = HybrisUtils::loadLibraryOS("libm.so", "libm.dylib", libm_symbols);
#else
    void* libmLib = HybrisUtils::loadLibraryOS("libm.so", "libm.so.6", libm_symbols);
#endif
    if (libmLib == nullptr)
        throw std::runtime_error("Failed to load libm");
    return libmLib;
}

void* MinecraftUtils::loadFMod() {
#ifdef __APPLE__
    void* fmodLib = HybrisUtils::loadLibraryOS("libfmod.so", PathHelper::findDataFile("libs/native/libfmod.dylib"), fmod_symbols);
#else
    void* fmodLib = HybrisUtils::loadLibraryOS("libfmod.so", PathHelper::findDataFile(std::string("lib/native/") + getLibraryAbi() + "/libfmod.so.10.20"), fmod_symbols);
#endif
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
    linker::init();

#ifndef USE_BIONIC_LIBC
    loadLibC();
    loadLibM();
#endif
    HybrisUtils::loadLibraryOS("libz.so", "libz.so.1", libz_symbols);
    HybrisUtils::hookAndroidLog();
    setupApi();
    // load stub libraries
#ifdef USE_BIONIC_LIBC
    if (!load_empty_library("ld-android.so") ||
        !hybris_dlopen(PathHelper::findDataFile("libs/hybris/libc.so").c_str(), 0) ||
        !hybris_dlopen(PathHelper::findDataFile("libs/hybris/libm.so").c_str(), 0))
        throw std::runtime_error("Failed to load Android libc.so/libm.so libraries");
#else
#endif
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

void* MinecraftUtils::loadMinecraftLib(std::string const& path) {
    // load gnustl_shared.so for <0.15.90.8
    std::string gnustlPath = FileUtil::getParent(path) + "/libgnustl_shared.so";
    if (FileUtil::exists(gnustlPath))
        linker::dlopen(gnustlPath.c_str(), 0);

    // load libc++_shared.so for 64-bit
    std::string cxxSharedPath = FileUtil::getParent(path) + "/libc++_shared.so";
    if (FileUtil::exists(cxxSharedPath))
        linker::dlopen(cxxSharedPath.c_str(), 0);

    void* handle = linker::dlopen(path.c_str(), 0);
    if (handle == nullptr)
        throw std::runtime_error(std::string("Failed to load Minecraft: ") + linker::dlerror());
    HookManager::instance.addLibrary(handle);
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
