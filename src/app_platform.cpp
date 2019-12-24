#include <mcpelauncher/app_platform.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <hybris/dlfcn.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <minecraft/Crypto.h>
#include <minecraft/legacy/UUID.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mcpelauncher/minecraft_version.h>
#include <random>
#ifdef HAS_LIBPNG
#include <png.h>
#endif

#ifdef __APPLE__
#include <stdint.h>
#include <sys/sysctl.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#else
#include <sys/sysinfo.h>
#endif

const char* LauncherAppPlatform::TAG = "AppPlatform";

void** LauncherAppPlatform::myVtable = nullptr;
size_t LauncherAppPlatform::myVtableSize;
bool enablePocketGuis = false;

LauncherAppPlatform::LauncherAppPlatform() : AppPlatform() {
    this->vtable = myVtable;
    dataDir = PathHelper::getPrimaryDataDirectory();
    assetsDir = PathHelper::findGameFile("assets/");
    tmpPath = PathHelper::getCacheDirectory();
    internalStorage = dataDir;
    externalStorage = dataDir;
    currentStorage = dataDir;
    userdata = dataDir;
    userdataPathForLevels = dataDir;
    region = "en_US";
}

void LauncherAppPlatform::initVtable(void* lib) {
    void** vt = AppPlatform::myVtable;
    void** vta = &((void**) hybris_dlsym(lib, "_ZTV19AppPlatform_android"))[2];
    myVtableSize = PatchUtils::getVtableSize(vta);
    Log::trace("AppPlatform", "Vtable size = %u", myVtableSize);

    myVtable = (void**) ::operator new((myVtableSize + 1) * sizeof(void*));
    myVtable[myVtableSize] = nullptr;
    memcpy(&myVtable[0], &vt[2], myVtableSize * sizeof(void*));

    PatchUtils::VtableReplaceHelper vtr (lib, myVtable, vta);
    vtr.replace("_ZNK19AppPlatform_android10getDataUrlEv", &LauncherAppPlatform::getDataUrl);
    vtr.replace("_ZNK19AppPlatform_android14getUserDataUrlEv", &LauncherAppPlatform::getUserDataUrl);
    vtr.replace("_ZN19AppPlatform_android14getPackagePathEv", &LauncherAppPlatform::getPackagePath);
    vtr.replace("_ZNK19AppPlatform_android14getPackagePathEv", &LauncherAppPlatform::getPackagePath);
    vtr.replace("_ZN11AppPlatform16hideMousePointerEv", &LauncherAppPlatform::hideMousePointer);
    vtr.replace("_ZN11AppPlatform16showMousePointerEv", &LauncherAppPlatform::showMousePointer);
    vtr.replace("_ZN19AppPlatform_android11swapBuffersEv", &LauncherAppPlatform::swapBuffers);
    vtr.replace("_ZNK19AppPlatform_android15getSystemRegionEv", &LauncherAppPlatform::getSystemRegion);
    vtr.replace("_ZN19AppPlatform_android25getGraphicsTearingSupportEv", &LauncherAppPlatform::getGraphicsTearingSupport);
    vtr.replace("_ZN19AppPlatform_android9pickImageESt10shared_ptrI20ImagePickingCallbackE", &LauncherAppPlatform::pickImage);
    vtr.replace("_ZN19AppPlatform_android9pickImageER20ImagePickingCallback", &LauncherAppPlatform::pickImageOld);
    vtr.replace("_ZN19AppPlatform_android8pickFileESt10shared_ptrI18FilePickerSettingsE", &LauncherAppPlatform::pickFile);
    vtr.replace("_ZN11AppPlatform8pickFileER18FilePickerSettings", &LauncherAppPlatform::pickFileOld);
    vtr.replace("_ZNK11AppPlatform19supportsFilePickingEv", &LauncherAppPlatform::supportsFilePicking);
    vtr.replace("_ZN19AppPlatform_android22getExternalStoragePathEv", &LauncherAppPlatform::getExternalStoragePath);
    vtr.replace("_ZNK19AppPlatform_android22getExternalStoragePathEv", &LauncherAppPlatform::getExternalStoragePath);
    vtr.replace("_ZN19AppPlatform_android22getInternalStoragePathEv", &LauncherAppPlatform::getInternalStoragePath);
    vtr.replace("_ZNK19AppPlatform_android22getInternalStoragePathEv", &LauncherAppPlatform::getInternalStoragePath);
    vtr.replace("_ZN19AppPlatform_android21getCurrentStoragePathEv", &LauncherAppPlatform::getCurrentStoragePath);
    vtr.replace("_ZNK19AppPlatform_android21getCurrentStoragePathEv", &LauncherAppPlatform::getCurrentStoragePath);
    vtr.replace("_ZN19AppPlatform_android15getUserdataPathEv", &LauncherAppPlatform::getUserdataPath);
    vtr.replace("_ZNK19AppPlatform_android15getUserdataPathEv", &LauncherAppPlatform::getUserdataPath);
    vtr.replace("_ZN19AppPlatform_android24getUserdataPathForLevelsEv", &LauncherAppPlatform::getUserdataPathForLevels);
    vtr.replace("_ZNK19AppPlatform_android24getUserdataPathForLevelsEv", &LauncherAppPlatform::getUserdataPathForLevels);
    vtr.replace("_ZN11AppPlatform20getAssetFileFullPathERKSs", &LauncherAppPlatform::getAssetFileFullPathOld);
    vtr.replace("_ZN11AppPlatform20getAssetFileFullPathERKN4Core4PathE", &LauncherAppPlatform::getAssetFileFullPath);
    vtr.replace("_ZNK11AppPlatform14useCenteredGUIEv", &LauncherAppPlatform::useCenteredGUI);
    vtr.replace("_ZN19AppPlatform_android16getApplicationIdEv", &LauncherAppPlatform::getApplicationId);
    vtr.replace("_ZNK19AppPlatform_android16getApplicationIdEv", &LauncherAppPlatform::getApplicationId);
    vtr.replace("_ZN19AppPlatform_android13getFreeMemoryEv", &LauncherAppPlatform::getFreeMemory); // legacy
    vtr.replace("_ZNK19AppPlatform_android13getFreeMemoryEv", &LauncherAppPlatform::getFreeMemory);
    vtr.replace("_ZN19AppPlatform_android13getUsedMemoryEv", &LauncherAppPlatform::getUsedMemory);
    vtr.replace("_ZN19AppPlatform_android22getTotalPhysicalMemoryEv", &LauncherAppPlatform::getTotalPhysicalMemory); // legacy
    vtr.replace("_ZNK19AppPlatform_android22getTotalPhysicalMemoryEv", &LauncherAppPlatform::getTotalPhysicalMemory);
    vtr.replace("_ZN19AppPlatform_android14getMemoryLimitEv", &LauncherAppPlatform::getMemoryLimit); // legacy
    vtr.replace("_ZNK19AppPlatform_android14getMemoryLimitEv", &LauncherAppPlatform::getMemoryLimit);
    vtr.replace("_ZN19AppPlatform_android11getDeviceIdEv", &LauncherAppPlatform::getDeviceId);
    vtr.replace("_ZN19AppPlatform_android18isFirstSnoopLaunchEv", &LauncherAppPlatform::isFirstSnoopLaunch);
    vtr.replace("_ZN19AppPlatform_android29hasHardwareInformationChangedEv", &LauncherAppPlatform::hasHardwareInformationChanged);
    vtr.replace("_ZN19AppPlatform_android8isTabletEv", &LauncherAppPlatform::isTablet);
    vtr.replace("_ZN11AppPlatform17setFullscreenModeE14FullscreenMode", &LauncherAppPlatform::setFullscreenMode);
    vtr.replace("_ZNK19AppPlatform_android10getEditionEv", &LauncherAppPlatform::getEdition);
    vtr.replace("_ZNK19AppPlatform_android16getBuildPlatformEv", &LauncherAppPlatform::getBuildPlatform);
    vtr.replace("_ZNK19AppPlatform_android17getPlatformStringEv", &LauncherAppPlatform::getPlatformString);
    vtr.replace("_ZNK19AppPlatform_android20getSubPlatformStringEv", &LauncherAppPlatform::getSubPlatformString);
    vtr.replace("_ZN19AppPlatform_android31calculateAvailableDiskFreeSpaceERKN4Core4PathE", &LauncherAppPlatform::calculateAvailableDiskFreeSpace);
    vtr.replace("_ZN19AppPlatform_android31calculateAvailableDiskFreeSpaceERKSs", &LauncherAppPlatform::calculateAvailableDiskFreeSpace);
    vtr.replace("_ZNK19AppPlatform_android25getPlatformUIScalingRulesEv", &LauncherAppPlatform::getPlatformUIScalingRules);
    vtr.replace("_ZN19AppPlatform_android19getPlatformTempPathEv", &LauncherAppPlatform::getPlatformTempPath);
    vtr.replace("_ZNK19AppPlatform_android19getPlatformTempPathEv", &LauncherAppPlatform::getPlatformTempPath);
    vtr.replace("_ZN19AppPlatform_android14createDeviceIDEv", &LauncherAppPlatform::createDeviceID_old);
    vtr.replace("_ZN19AppPlatform_android14createDeviceIDERSs", &LauncherAppPlatform::createDeviceID);
    vtr.replace("_ZN19AppPlatform_android18queueForMainThreadESt8functionIFvvEE", &LauncherAppPlatform::queueForMainThread);
    vtr.replace("_ZN11AppPlatform16allowSplitScreenEv", &LauncherAppPlatform::allowSplitScreen);
    vtr.replace("_ZN19AppPlatform_android21calculateHardwareTierEv", &LauncherAppPlatform::calculateHardwareTier);
    vtr.replace("_ZNK11AppPlatform17supportsScriptingEv", &LauncherAppPlatform::supportsScripting);
    vtr.replace("_ZNK19AppPlatform_android17supportsScriptingEv", &LauncherAppPlatform::supportsScripting);
    vtr.replace("_ZN19AppPlatform_android21getBroadcastAddressesEv", &LauncherAppPlatform::getBroadcastAddresses);
    vtr.replace("_ZNK11AppPlatform16supports3DExportEv", &LauncherAppPlatform::supports3DExport);
    vtr.replace("_ZNK19AppPlatform_android21getPlatformTTSEnabledEv", &LauncherAppPlatform::getPlatformTTSEnabled);
    vtr.replace("_ZN19AppPlatform_android10createUUIDEv", &LauncherAppPlatform::createUUID);
    vtr.replace("_ZNK11AppPlatform10getEditionEv", &LauncherAppPlatform::getEdition);
    vtr.replace("_ZNK19AppPlatform_android15getSystemLocaleEv", &LauncherAppPlatform::getSystemLocale);
    vtr.replace("_ZNK19AppPlatform_android16getApplicationIdEv", &LauncherAppPlatform::getApplicationId);

    vtr.replace("_ZN19AppPlatform_android35getMultiplayerServiceListToRegisterEv", hybris_dlsym(lib, "_ZN19AppPlatform_android35getMultiplayerServiceListToRegisterEv"));
    vtr.replace("_ZN19AppPlatform_android36getBroadcastingMultiplayerServiceIdsEbb", hybris_dlsym(lib, "_ZN19AppPlatform_android36getBroadcastingMultiplayerServiceIdsEbb"));

    if (!MinecraftVersion::isAtLeast(1, 14))
        vtr.replace("_ZN11AppPlatform20getAssetFileFullPathERKN4Core4PathE", &LauncherAppPlatform::getAssetFileFullPath_pre_1_14);
    if (!MinecraftVersion::isAtLeast(1, 13))
        vtr.replace("_ZN11AppPlatform20getAssetFileFullPathERKN4Core4PathE", &LauncherAppPlatform::getAssetFileFullPath_pre_1_13);

    if (MinecraftVersion::isAtLeast(1, 13, 0, 9))
        vtr.replace("_ZN19AppPlatform_android13readAssetFileERKN4Core4PathE", &LauncherAppPlatform::readAssetFile);
    if (!MinecraftVersion::isAtLeast(1, 14))
        vtr.replace("_ZN19AppPlatform_android13readAssetFileERKN4Core4PathE", &LauncherAppPlatform::readAssetFile_pre_1_14);
    if (!MinecraftVersion::isAtLeast(0, 16))
        vtr.replace("_ZN19AppPlatform_android13readAssetFileERKSs", &LauncherAppPlatform::readAssetFile_pre_0_16);

    // < 0.15
    if (!MinecraftVersion::isAtLeast(0, 14, 99))
        vtr.replace("_ZN19AppPlatform_android13readAssetFileERKSs", &LauncherAppPlatform::readAssetFile_pre_0_15);
    vtr.replace("_ZN19AppPlatform_android12getImagePathERKSs15TextureLocation", &LauncherAppPlatform::getImagePath_pre_0_15);
    vtr.replace("_ZNK19AppPlatform_android13getScreenTypeEv", &LauncherAppPlatform::getScreenType);
    vtr.replace("_ZN19AppPlatform_android17getGraphicsVendorEv", &LauncherAppPlatform::getGraphicsVendor_pre_0_15);
    vtr.replace("_ZN19AppPlatform_android19getGraphicsRendererEv", &LauncherAppPlatform::getGraphicsRenderer_pre_0_15);
    vtr.replace("_ZN19AppPlatform_android18getGraphicsVersionEv", &LauncherAppPlatform::getGraphicsVersion_pre_0_15);
    vtr.replace("_ZN19AppPlatform_android21getGraphicsExtensionsEv", &LauncherAppPlatform::getGraphicsExtensions_pre_0_15);

    // < 0.14.2
    vtr.replace("_ZN19AppPlatform_android12getImagePathERKSsb", &LauncherAppPlatform::getImagePath_pre_0_14_2);

    // < 0.14
    vtr.replace("_ZN19AppPlatform_android7loadPNGER9ImageDataRKSsb", &LauncherAppPlatform::loadPNG_pre_0_14);
}

long long LauncherAppPlatform::calculateAvailableDiskFreeSpace() {
    struct statvfs buf;
    statvfs(dataDir.c_str(), &buf);
    return (long long int) buf.f_bsize * buf.f_bfree;
}

long long LauncherAppPlatform::getUsedMemory() {
#ifdef __APPLE__
    uint64_t page_size;
    size_t len = sizeof(page_size);
    sysctlbyname("hw.pagesize", &page_size, &len, NULL, 0);

    struct vm_statistics64 stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t) &stat, &count);

    double page_K = page_size / (double) 1024;
    return (stat.active_count + stat.wire_count) * page_K * 1000;
#else
    FILE* file = fopen("/proc/self/statm", "r");
    if (file == nullptr)
        return 0L;
    int pageSize = getpagesize();
    long long pageCount = 0L;
    fscanf(file, "%lld", &pageCount);
    fclose(file);
    return pageCount * pageSize;
#endif
}

long long LauncherAppPlatform::getFreeMemory() {
#ifdef __APPLE__
    uint64_t page_size;
    size_t len = sizeof(page_size);
    sysctlbyname("hw.pagesize", &page_size, &len, NULL, 0);

    struct vm_statistics64 stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t) &stat, &count);

    double page_K = page_size / (double) 1024;
    return stat.free_count * page_K * 1000;
#else
    struct sysinfo memInfo;
    sysinfo (&memInfo);
    long long total = memInfo.freeram;
    total *= memInfo.mem_unit;
    return total;
#endif
}

long long LauncherAppPlatform::getTotalPhysicalMemory() {
#ifdef __APPLE__
    uint64_t memsize;
    size_t len = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &len, NULL, 0);
    return memsize;
#else
    struct sysinfo memInfo;
    sysinfo (&memInfo);
    long long total = memInfo.totalram;
    total *= memInfo.mem_unit;
    return total;
#endif
}

long long LauncherAppPlatform::getMemoryLimit() {
    return getTotalPhysicalMemory();
}

void LauncherAppPlatform::calculateHardwareTier() {
    hardwareTier = 3;
}

std::vector<mcpe::string> LauncherAppPlatform::getBroadcastAddresses() {
    struct ifaddrs *ifaddrs = nullptr;
    if (getifaddrs(&ifaddrs) != 0)
        return std::vector<mcpe::string>();
    std::vector<mcpe::string> retval;
    for (struct ifaddrs *ifaddr = ifaddrs; ifaddr; ifaddr = ifaddr->ifa_next) {
        if (!(ifaddr->ifa_flags & IFF_BROADCAST))
            continue;
#ifdef __APPLE__
        auto addr = ifaddr->ifa_dstaddr;
#else
        auto addr = ifaddr->ifa_ifu.ifu_broadaddr;
#endif
        if (addr == nullptr)
            continue;
        if (addr->sa_family == AF_INET) {
            char buf[INET_ADDRSTRLEN] = {0};
            if (!inet_ntop(addr->sa_family, &((sockaddr_in*) addr)->sin_addr, buf, sizeof(buf)))
                continue;
            retval.emplace_back(buf);
        }
        if (addr->sa_family == AF_INET6) {
            char buf[INET6_ADDRSTRLEN] = {0};
            if (!inet_ntop(addr->sa_family, &((sockaddr_in6*) addr)->sin6_addr, buf, sizeof(buf)))
                continue;
            retval.emplace_back(buf);
        }
    }
    freeifaddrs(ifaddrs);
    return retval;
}

mcpe::string LauncherAppPlatform::createDeviceID(mcpe::string &error) {
    auto deviceIdFile = PathHelper::getPrimaryDataDirectory() + "deviceid.txt";
    {
        std::ifstream in (deviceIdFile);
        if (in) {
            std::string deviceId;
            if (std::getline(in, deviceId) && !in.eof() && !deviceId.empty()) {
                Log::trace(TAG, "createDeviceID (from file): %s", deviceId.c_str());
                return deviceId;
            }
        }
    }
    auto deviceId = Crypto::Random::generateUUID().asString();
    {
        std::ofstream out (deviceIdFile);
        out << deviceId << std::endl;
    }
    Log::trace(TAG, "createDeviceID (created new): %s", deviceId.c_str());
    return deviceId;
}

mcpe::string LauncherAppPlatform::createUUID() {
    if (!MinecraftVersion::isAtLeast(0, 14, 99)) {
        static std::independent_bits_engine<std::random_device, CHAR_BIT, unsigned char> engine;
        unsigned char rawBytes[16];
        std::generate(rawBytes, rawBytes + 16, std::ref(engine));
        rawBytes[6] = (rawBytes[6] & (unsigned char) 0x0F) | (unsigned char) 0x40;
        rawBytes[8] = (rawBytes[6] & (unsigned char) 0x3F) | (unsigned char) 0x80;
        mcpe::string ret;
        ret.resize(36);
        snprintf((char*) ret.c_str(), 36, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                rawBytes[0], rawBytes[1], rawBytes[2], rawBytes[3],
                rawBytes[4], rawBytes[5], rawBytes[6], rawBytes[7], rawBytes[8], rawBytes[9],
                rawBytes[10], rawBytes[11], rawBytes[12], rawBytes[13], rawBytes[14], rawBytes[15]);
        return ret;
    }
    auto uuid = Crypto::Random::generateUUID();
    if (!MinecraftVersion::isAtLeast(0, 16))
        return ((Legacy::Pre_1_0_4::mce::UUID*) &uuid)->toString();
    return uuid.asString();
}

mcpe::string LauncherAppPlatform::readAssetFileImpl(const char *p) {
    // the function reimplements readAssetFile; this is required because MCPE tries to open directories, which crashes
    int fd = open(p, O_RDONLY);
    if (fd < 0) {
        Log::error("LauncherAppPlatform", "readAssetFile: not found: %s", p);
        return mcpe::string();
    }
    struct stat sr;
    if (fstat(fd, &sr) < 0 || (sr.st_mode & S_IFDIR)) {
        close(fd);
        Log::error("LauncherAppPlatform", "readAssetFile: opening a directory: %s", p);
        return mcpe::string();
    }
    auto size = lseek(fd, 0, SEEK_END);
    if (size == (off_t) -1) {
        Log::error("LauncherAppPlatform", "readAssetFile: lseek error");
        close(fd);
        return mcpe::string();
    }
    mcpe::string ret;
    ret.resize((std::size_t) size);
    lseek(fd, 0, SEEK_SET);
    for (size_t o = 0; o < size; ) {
        int res = read(fd, (char*) &ret.c_str()[o], size - o);
        if (res < 0) {
            Log::error("LauncherAppPlatform", "readAssetFile: read error");
            close(fd);
            return mcpe::string();
        }
        o += res;
    }
    close(fd);
    return ret;
}

void LauncherAppPlatform::loadPNG_pre_0_14(Legacy::Pre_0_14::ImageData &imgData, mcpe::string const &path, bool b) {
#ifdef HAS_LIBPNG
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        Log::error("LauncherAppPlatform", "loadPNG: failed to open file %s", path.c_str());
        return;
    }

    png_structp png = nullptr;
    png_infop info = nullptr;
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (setjmp(png_jmpbuf(png))) {
        Log::error("LauncherAppPlatform", "loadPNG: failed to read png");
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(file);
        return;
    }

    if (!png) {
        Log::error("LauncherAppPlatform", "loadPNG: png alloc failed");
        longjmp(png_jmpbuf(png), 1);
    }
    info = png_create_info_struct(png);
    if (!info) {
        Log::error("LauncherAppPlatform", "loadPNG: png info struct alloc failed");
        longjmp(png_jmpbuf(png), 1);
    }

    png_init_io(png, file);

    png_read_info(png, info);

    imgData.w = (int) png_get_image_width(png, info);
    imgData.h = (int) png_get_image_height(png, info);
    imgData.format = Legacy::Pre_0_14::TextureFormat::U8888;
    imgData.mipLevel = 0;

    png_byte bitDepth = png_get_bit_depth(png, info);
    png_byte colorType = png_get_color_type(png, info);
    if (colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_RGB) {
        if (png_get_valid(png, info, PNG_INFO_tRNS))
            png_set_tRNS_to_alpha(png);
        else
            png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (bitDepth == 16)
        png_set_strip_16(png);
    png_read_update_info(png, info);

    png_size_t rowBytes = png_get_rowbytes(png, info);

    imgData.data.resize(rowBytes * imgData.h);

    png_byte* rows[imgData.h];
    for (int i = 0; i < imgData.h; i++) {
        rows[i] = (png_byte*) &(imgData.data.c_str())[i * rowBytes];
    }
    png_read_image(png, rows);

    fclose(file);
    png_destroy_read_struct(&png, &info, nullptr);
#else
    Log::error("LauncherAppPlatform", "loadPNG: stubbed due to no libpng support at compile time");
#endif
}