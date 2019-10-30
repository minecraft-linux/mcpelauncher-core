#pragma once

#include <string>
#include <functional>
#include <unistd.h>
#include <sys/param.h>
#include <vector>
#include <mutex>
#include <memory>
#include <log.h>
#include <mcpelauncher/path_helper.h>
#include "patch_utils.h"
#include <minecraft/gl.h>
#include <minecraft/AppPlatform.h>
#include <minecraft/Core.h>
#include <minecraft/std/function.h>
#include <minecraft/std/shared_ptr.h>
#include <minecraft/legacy/FilePickerSettings.h>
#include <minecraft/legacy/ImageData.h>
#include <minecraft/legacy/Core.h>

class ImagePickingCallback;
class FilePickerSettings;
class GameWindow;

extern bool enablePocketGuis;

class LauncherAppPlatform : public AppPlatform {

private:
    static const char* TAG;

public:
    static void** myVtable;
    static size_t myVtableSize;
    static void initVtable(void* lib);

    mcpe::string region;
    mcpe::string internalStorage, externalStorage, currentStorage, userdata, userdataPathForLevels, tmpPath;

    std::string assetsDir, dataDir;

    std::vector<mcpe::function<void ()>> runOnMainThreadQueue;

    std::mutex runOnMainThreadMutex;

    LauncherAppPlatform();

    mcpe::string getDataUrl() { // this is used only for sounds
        Log::trace(TAG, "getDataUrl: %s", assetsDir.c_str());
        return assetsDir;
    }
    mcpe::string getUserDataUrl() {
        Log::trace(TAG, "getUserDataUrl: %s", dataDir.c_str());
        return dataDir;
    }

    mcpe::string getPackagePath() {
        return assetsDir;
    }

    void hideMousePointer() {}
    void showMousePointer() {}

    void swapBuffers() {
        //printf("swap buffers\n");
    }
    mcpe::string const& getSystemRegion() {
        Log::trace(TAG, "getSystemRegion: %s", region.c_str());
        return region;
    }

    bool getGraphicsTearingSupport() {
        return false;
    }

    void pickImage(mcpe::shared_ptr<ImagePickingCallback>) {}
    void pickImageOld(ImagePickingCallback& callback) {}
    void pickFile(mcpe::shared_ptr<FilePickerSettings> callback) {}
    void pickFileOld(Legacy::Pre_1_8::FilePickerSettings& callback) {}
    bool supportsFilePicking() { return false; }
    mcpe::string& getExternalStoragePath() {
        // Log::trace(TAG, "getExternalStoragePath: %s", externalStorage.c_str());
        return externalStorage;
    }
    mcpe::string& getInternalStoragePath() {
        // Log::trace(TAG, "getInternalStoragePath: %s", internalStorage.c_str());
        return internalStorage;
    }
    mcpe::string& getCurrentStoragePath() {
        // Log::trace(TAG, "getCurrentStoragePath: %s", currentStorage.c_str());
        return currentStorage;
    }
    mcpe::string& getUserdataPath() {
        // Log::trace(TAG, "getUserdataPath: %s", userdata.c_str());
        return userdata;
    }
    mcpe::string& getUserdataPathForLevels() {
        // Log::trace(TAG, "getUserdataPathForLevels: %s", userdataPathForLevels.c_str());
        return userdataPathForLevels;
    }
    mcpe::string getAssetFileFullPath(Core::Path const& s) {
        // Log::trace(TAG, "getAssetFileFullPath: %s", s.c_str());
        std::string ret = assetsDir;
        if (s.hasSize)
            ret.append(s.path, s.path + s.size);
        else
            ret.append(s.path);
        return mcpe::string(ret);
    }
    mcpe::string getAssetFileFullPath_pre_1_13(Legacy::Pre_1_13::Core::Path const& s) {
        // Log::trace(TAG, "getAssetFileFullPath: %s", s.c_str());
        std::string ret = assetsDir;
        if (s.size != (size_t) -1)
            ret.append(s.path, s.path + s.size);
        else
            ret.append(s.path);
        return mcpe::string(ret);
    }
    mcpe::string getAssetFileFullPathOld(mcpe::string const& s) {
        // Log::trace(TAG, "getAssetFileFullPath: %s", s.c_str());
        return mcpe::string(assetsDir + s.std());
    }
    int getScreenType() {
        if (enablePocketGuis)
            return 1;
        return 0; // Win 10 Ed. GUIs
    }
    bool useCenteredGUI() {
        return (enablePocketGuis ? false : true);
    }
    mcpe::string getApplicationId() {
        Log::trace(TAG, "getApplicationId: com.mojang.minecraftpe");
        return "com.mojang.minecraftpe";
    }
    mcpe::string getDeviceId() {
        Log::trace(TAG, "getDeviceId: linux");
        return "linux";
    }
    bool isFirstSnoopLaunch() {
        Log::trace(TAG, "isFirstSnoopLaunch: true");
        return true;
    }
    bool hasHardwareInformationChanged() {
        Log::trace(TAG, "hasHardwareInformationChanged: false");
        return false;
    }
    bool isTablet() {
        Log::trace(TAG, "isTablet: true");
        return true;
    }
    void setFullscreenMode(int mode) {}
    mcpe::string getEdition() {
        if (enablePocketGuis)
            return "pocket";
        return "win10";
    }
    int getBuildPlatform() const {
        return 1;
    }
    mcpe::string getPlatformString() const {
        return "Linux";
    }
    mcpe::string getSubPlatformString() const {
        return "Linux";
    }
    int getPlatformUIScalingRules() {
        return enablePocketGuis ? 2 : 0;
    }
    long long getUsedMemory();
    long long getFreeMemory();
    long long getTotalPhysicalMemory();
    long long getMemoryLimit();

    long long calculateAvailableDiskFreeSpace();

    mcpe::string &getPlatformTempPath() {
        return tmpPath;
    }

    mcpe::string createDeviceID_old() {
        return "linux";
    }

    mcpe::string createDeviceID(mcpe::string& error);

    mcpe::string createUUID();

    bool allowSplitScreen() {
        return true;
    }

    void queueForMainThread(mcpe::function<void ()> f) {
        runOnMainThreadMutex.lock();
        runOnMainThreadQueue.push_back(f);
        runOnMainThreadMutex.unlock();
    }
    void runMainThreadTasks() {
        runOnMainThreadMutex.lock();
        auto queue = std::move(runOnMainThreadQueue);
        runOnMainThreadMutex.unlock();
        for (auto const& func : queue)
            func();
    }

    void calculateHardwareTier();

    bool supportsScripting() {
        return true;
    }

    bool supports3DExport() {
        return true;
    }

    bool getPlatformTTSEnabled() {
        return false;
    }

    std::vector<mcpe::string> getBroadcastAddresses();

    mcpe::string systemLocale = "en";

    mcpe::string const &getSystemLocale() {
        return systemLocale;
    }

    mcpe::string readAssetFile(Core::Path const &p);

    mcpe::string readAssetFile_pre_0_16(mcpe::string const& path);

    mcpe::string readAssetFile_pre_0_15(mcpe::string const& path) {
        return readAssetFile_pre_0_16(assetsDir + path.std());
    }

    mcpe::string getImagePath_pre_0_15(mcpe::string const& s, int loc) {
        if (loc == 0)
            return assetsDir + "images/" + s.std();
        else
            return assetsDir + s.std();
    }
    mcpe::string getImagePath_pre_0_14_2(mcpe::string const& s, bool b) {
        return getImagePath_pre_0_15(s, b ? 0 : 1);
    }
    mcpe::string getGraphicsVendor_pre_0_15() {
        return gl::getOpenGLVendor();
    }
    mcpe::string getGraphicsRenderer_pre_0_15() {
        return gl::getOpenGLRenderer();
    }
    mcpe::string getGraphicsVersion_pre_0_15() {
        return gl::getOpenGLVersion();
    }
    mcpe::string getGraphicsExtensions_pre_0_15() {
        return gl::getOpenGLExtensions();
    }

    void loadPNG_pre_0_14(Legacy::Pre_0_14::ImageData& imgData, mcpe::string const& path, bool b);

};
