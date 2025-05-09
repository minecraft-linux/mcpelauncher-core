#include <mcpelauncher/fmod_utils.h>
#include <mcpelauncher/linker.h>

int (*FmodUtils::FMOD_System_Init)(void* system, int maxchannels, unsigned int flags, void* extradriverdata) = nullptr;
int (*FmodUtils::FMOD_System_SetSoftwareFormat)(void* system, int samplerate, int speakermode, int numrawspeakers) = nullptr;
int (*FmodUtils::FMOD_System_SetDSPBufferSize)(void* system, unsigned int bufferlength, int numbuffers);
int (*FmodUtils::FMOD_System_GetDSPBufferSize)(void* system, unsigned int* bufferlength, int* numbuffers);

int FmodUtils::sampleRate = 48000;

static int ReadEnvInt(const char* name, int def = 0) {
    auto val = getenv(name);
    if(!val) {
        return def;
    }
    std::string sval = val;
    return std::stoi(sval);
}

bool FmodUtils::setup(void* handle) {
    FMOD_System_Init = (decltype(FMOD_System_Init))linker::dlsym(handle, "_ZN4FMOD6System4initEijPv");
    FMOD_System_SetSoftwareFormat = (decltype(FMOD_System_SetSoftwareFormat))linker::dlsym(handle, "_ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi");
    FMOD_System_SetDSPBufferSize = (decltype(FMOD_System_SetDSPBufferSize))linker::dlsym(handle, "_ZN4FMOD6System16setDSPBufferSizeEji");
    FMOD_System_GetDSPBufferSize = (decltype(FMOD_System_GetDSPBufferSize))linker::dlsym(handle, "_ZN4FMOD6System16getDSPBufferSizeEPjPi");

    return FMOD_System_Init != NULL && FMOD_System_SetSoftwareFormat != NULL && FMOD_System_SetDSPBufferSize != NULL && FMOD_System_GetDSPBufferSize != NULL;
}

int FmodUtils::initHook(void* system, int maxchannels, unsigned int flags, void* extradriverdata) {
    unsigned int defaultBufferLen;
    int defaultNumBuffers;
    FMOD_System_GetDSPBufferSize(system, &defaultBufferLen, &defaultNumBuffers);
    FMOD_System_SetDSPBufferSize(system, ReadEnvInt("FMOD_DSP_BUFFER_LENGTH", defaultBufferLen), ReadEnvInt("FMOD_DSP_NUM_BUFFERS", defaultNumBuffers));
    FMOD_System_SetSoftwareFormat(system, sampleRate, ReadEnvInt("FMOD_SPEAKER_MODE", 0), 0);
    return FMOD_System_Init(system, maxchannels, flags, extradriverdata);
}
