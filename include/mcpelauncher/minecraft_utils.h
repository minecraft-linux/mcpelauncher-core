#pragma once

#include <mcpelauncher/path_helper.h>
#include <unordered_map>
#include <mcpelauncher/linker.h>

class MinecraftUtils {
private:
    static void setupApi();

    struct HookEntry {
        void* value;
        void* user;
        void (*callback)(void*, void*);
    };

    static std::unordered_map<std::string, HookEntry> preinitHooks;

public:
    static std::unordered_map<std::string, void*> getApi();

    static void workaroundLocaleBug();

    static std::unordered_map<std::string, void*> getLibCSymbols();
    static void* loadLibM();

    static void setupHybris();

    static void* loadMinecraftLib(void* showMousePointerCallback = nullptr, void* hideMousePointerCallback = nullptr, void* fullscreenCallback = nullptr, void* closeCallback = nullptr, std::vector<mcpelauncher_hook_t> hooks = {});

    static void* loadFMod();
    static void stubFMod();

    static const char* getLibraryAbi();

    static size_t getLibraryBase(void* handle);

    static void setupGLES2Symbols(void* (*resolver)(const char*));
};
