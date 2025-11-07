#pragma once

#include <string>

class MinecraftVersion {

public:
    static int major;
    static int minor;
    static int patch;
    static int revision;
    static int code;
    static std::string package;

    static void init(std::string package, int versionCode);

    static bool isAtLeast(int major, int minor, int patch = -1, int revision = -1) {
        return MinecraftVersion::major > major || (MinecraftVersion::major == major &&
            (MinecraftVersion::minor > minor || (MinecraftVersion::minor == minor &&
            (MinecraftVersion::patch > patch || (MinecraftVersion::patch == patch &&
            MinecraftVersion::revision >= revision)))));
    }

    static bool isExactly(int major, int minor, int patch, int revision) {
        return MinecraftVersion::major == major && MinecraftVersion::minor == minor &&
                MinecraftVersion::revision == revision && MinecraftVersion::patch == patch;
    }

    static std::string getString();

};