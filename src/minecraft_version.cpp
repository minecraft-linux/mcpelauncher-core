#include <mcpelauncher/minecraft_version.h>
#include <sstream>

int MinecraftVersion::major;
int MinecraftVersion::minor;
int MinecraftVersion::patch;
int MinecraftVersion::revision;
int MinecraftVersion::code;
std::string MinecraftVersion::package;

void MinecraftVersion::init(std::string package, int versionCode) {
    major = minor = patch = revision = 0;
    code = versionCode;
    MinecraftVersion::package = package;

    // 962112004 = 1.21.120.4
    bool isandroid = versionCode >= 950000000 && versionCode < 990000000;
    bool ischromeos = versionCode >= 1950000000 && versionCode < 1990000000;
    if(isandroid || ischromeos) { // most modern version schema
        int parts = versionCode % 10000000;
        major = 1;
        minor = parts / 100000;
        parts = parts % 100000;
        patch = parts / 100;
        parts = parts % 100;
        revision = parts;
    }
}

std::string MinecraftVersion::getString() {
    std::stringstream ss;
    ss << major << '.' << minor << '.' << patch << '.' << revision;
    return ss.str();
}