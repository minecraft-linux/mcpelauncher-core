#include <mcpelauncher/minecraft_version.h>

int MinecraftVersion::major;
int MinecraftVersion::minor;
int MinecraftVersion::patch;
int MinecraftVersion::revision;

void MinecraftVersion::init() {
    minecraft_get_version(&major, &minor, &patch, &revision);
}