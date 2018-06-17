#include <mcpelauncher/minecraft_utils.h>
#include <mcpelauncher/patch_utils.h>
#include <mcpelauncher/hybris_utils.h>
#include <mcpelauncher/hook.h>
#include <mcpelauncher/path_helper.h>
#include <minecraft/imported/android_symbols.h>
#include <minecraft/imported/egl_symbols.h>
#include <minecraft/imported/libm_symbols.h>
#include <minecraft/imported/fmod_symbols.h>
#include <minecraft/symbols.h>
#include <minecraft/std/string.h>
#include <log.h>
#include <hybris/dlfcn.h>
#include <hybris/hook.h>
#include <stdexcept>

extern "C" {
#include <hybris/jb/linker.h>
}

void MinecraftUtils::workaroundLocaleBug() {
    setenv("LC_ALL", "C", 1); // HACK: Force set locale to one recognized by MCPE so that the outdated C++ standard library MCPE uses doesn't fail to find one
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
    void* fmodLib = HybrisUtils::loadLibraryOS(PathHelper::findDataFile("libs/native/libfmod.so.9.6"), fmod_symbols);
#endif
    if (fmodLib == nullptr)
        throw std::runtime_error("Failed to load fmod");
    return fmodLib;
}

void MinecraftUtils::stubFMod() {
    HybrisUtils::stubSymbols(fmod_symbols, (void*) (void (*)()) []() {
        Log::warn("Launcher", "FMod stub called");
    });
}

void MinecraftUtils::setupHybris() {
    loadLibM();
    HybrisUtils::stubSymbols(android_symbols, (void*) (void (*)()) []() {
        Log::warn("Launcher", "Android stub called");
    });
    HybrisUtils::stubSymbols(egl_symbols, (void*) (void (*)()) []() {
        Log::warn("Launcher", "EGL stub called");
    });
    HybrisUtils::hookAndroidLog();
    hybris_hook("mcpelauncher_hook", (void*) HookManager::hookFunction);
    // load stub libraries
    if (!load_empty_library("libc.so") || !load_empty_library("libm.so"))
        throw std::runtime_error("Failed to load stub libraries");
    if (!load_empty_library("libandroid.so") || !load_empty_library("liblog.so") || !load_empty_library("libEGL.so") || !load_empty_library("libGLESv2.so") || !load_empty_library("libOpenSLES.so") || !load_empty_library("libfmod.so") || !load_empty_library("libGLESv1_CM.so"))
        throw std::runtime_error("Failed to load stub libraries");
    if (!load_empty_library("libmcpelauncher_mod.so"))
        throw std::runtime_error("Failed to load stub libraries");
    load_empty_library("libstdc++.so");
}

void* MinecraftUtils::loadMinecraftLib() {
    std::string mcpePath = PathHelper::findDataFile("libs/libminecraftpe.so");
    void* handle = hybris_dlopen(mcpePath.c_str(), RTLD_LAZY);
    if (handle == nullptr)
        throw std::runtime_error(std::string("Failed to load Minecraft: %s") + hybris_dlerror());
    HookManager::addHookLibrary(handle, mcpePath);
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

void MinecraftUtils::initSymbolBindings(void* handle) {
    mcpe::string::empty = (mcpe::string*) hybris_dlsym(handle, "_ZN4Util12EMPTY_STRINGE");
    minecraft_symbols_init(handle);
}

static void workerPoolDestroy(void* th) {
    Log::trace("Launcher", "WorkerPool-related class destroy %lu", (unsigned long) th);
}
void MinecraftUtils::workaroundShutdownCrash(void* handle) {
    // this is an ugly hack to workaround the close app crashes MCPE causes
    unsigned int patchOff = (unsigned int) hybris_dlsym(handle, "_ZN9TaskGroupD2Ev");
    PatchUtils::patchCallInstruction((void*) patchOff, (void*) &workerPoolDestroy, true);
    patchOff = (unsigned int) hybris_dlsym(handle, "_ZN10WorkerPoolD2Ev");
    PatchUtils::patchCallInstruction((void*) patchOff, (void*) &workerPoolDestroy, true);
    patchOff = (unsigned int) hybris_dlsym(handle, "_ZN9SchedulerD2Ev");
    PatchUtils::patchCallInstruction((void*) patchOff, (void*) &workerPoolDestroy, true);
}
