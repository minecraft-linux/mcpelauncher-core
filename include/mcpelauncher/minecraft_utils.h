#pragma once

#include <mcpelauncher/path_helper.h>

class MinecraftUtils {

private:
    static void* loadLibC();
    static void* loadLibM();

    static void setupApi();

public:
    static void workaroundLocaleBug();

    static void setupHybris();

    static void* loadMinecraftLib(std::string const& path = PathHelper::findGameFile("libs/libminecraftpe.so"));

    static void* loadFMod();
    static void stubFMod();

    static size_t getLibraryBase(void* handle);

    static void setupGLES2Symbols(void* (*resolver)(const char*));

};
