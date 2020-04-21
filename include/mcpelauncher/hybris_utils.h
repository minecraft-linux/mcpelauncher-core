#pragma once

#include <string>

class HybrisUtils {

private:
    static const char* TAG;

public:
    static bool loadLibrary(std::string path);
    static void* loadLibraryOS(const char *name, std::string const &path, const char** symbols);

    static void stubSymbols(const char *name, const char** symbols, void* stubfunc);

private:
    friend class MinecraftUtils;

    static void hookAndroidLog();

};