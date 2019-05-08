#pragma once

#include <minecraft/SharedConstants.h>
#include <minecraft/legacy/SharedConstants.h>

class MinecraftVersion {

private:
    static inline int Revision() {
        return *(SharedConstants::RevisionVersion != nullptr ?
            SharedConstants::RevisionVersion : Legacy::Pre_0_17::SharedConstants::BetaVersion);
    }

public:
    static bool isAtLeast(int major, int minor, int patch = -1, int revision = -1) {
        return *SharedConstants::MajorVersion > major || (*SharedConstants::MajorVersion == major &&
            (*SharedConstants::MinorVersion > minor || (*SharedConstants::MinorVersion == minor &&
            (*SharedConstants::PatchVersion > patch || (*SharedConstants::PatchVersion == patch &&
            Revision() >= revision)))));
    }

    static bool isExactly(int major, int minor, int patch, int revision) {
        return *SharedConstants::MajorVersion == major && *SharedConstants::MinorVersion == minor &&
                Revision() == revision && *SharedConstants::PatchVersion == patch;
    }

};