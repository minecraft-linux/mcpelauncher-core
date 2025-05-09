#pragma once

class FmodUtils {
private:
    static int (*FMOD_System_Init)(void* system, int maxchannels, unsigned int flags, void* extradriverdata);
    static int (*FMOD_System_SetSoftwareFormat)(void* system, int samplerate, int speakermode, int numrawspeakers);
    static int (*FMOD_System_SetDSPBufferSize)(void* system, unsigned int bufferlength, int numbuffers);
    static int (*FMOD_System_GetDSPBufferSize)(void* system, unsigned int* bufferlength, int* numbuffers);

    static int sampleRate;

public:
    static bool setup(void* handle);
    static int initHook(void* system, int maxchannels, unsigned int flags, void* extradriverdata);
    static void setSampleRate(int newSampleRate) {
        sampleRate = newSampleRate;
    };
};