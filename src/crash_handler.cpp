#include <mcpelauncher/crash_handler.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <cxxabi.h>
#include <execinfo.h>
#include <mcpelauncher/linker.h>


bool CrashHandler::hasCrashed = false;

void CrashHandler::handleSignal(int signal, void *aptr) {
    printf("Signal %i received\n", signal);

    struct sigaction act;
    act.sa_handler = nullptr;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGSEGV, &act, 0);
    sigaction(SIGABRT, &act, 0);

    if (hasCrashed)
        return;
    hasCrashed = true;
    void** ptr = &aptr;
    void *array[25];
    int count = backtrace(array, 25);
    char **symbols = backtrace_symbols(array, count);
    char *nameBuf = (char*) malloc(256);
    size_t nameBufLen = 256;
    printf("Backtrace elements: %i\n", count);
    for (int i = 0; i < count; i++) {
        if (symbols[i] == nullptr) {
            printf("#%i unk [0x%4p]\n", i, array[i]);
            continue;
        }
        if (symbols[i][0] == '[') { // unknown symbol
            Dl_info symInfo;
            if (linker::dladdr(array[i], &symInfo)) {
                int status = 0;
                nameBuf = abi::__cxa_demangle(symInfo.dli_sname, nameBuf, &nameBufLen, &status);
                printf("#%i HYBRIS %s+%p in %s+0x%4p [0x%4p]\n", i, nameBuf, (void *) ((size_t) array[i] - (size_t) symInfo.dli_saddr), symInfo.dli_fname, (void *) ((size_t) array[i] - (size_t) symInfo.dli_fbase), array[i]);
                continue;
            }
        }
        printf("#%i %s\n", i, symbols[i]);
    }
    printf("Dumping stack...\n");
    for (int i = 0; i < 1000; i++) {
        void* pptr = *ptr;
        Dl_info symInfo;
        if (linker::dladdr(pptr, &symInfo)) {
            int status = 0;
            nameBuf = abi::__cxa_demangle(symInfo.dli_sname, nameBuf, &nameBufLen, &status);
            printf("#%i HYBRIS %s+%p in %s+0x%4p [0x%4p]\n", i, nameBuf, (void *) ((size_t) pptr - (size_t) symInfo.dli_saddr), symInfo.dli_fname, (void *) ((size_t) pptr - (size_t) symInfo.dli_fbase), pptr);
        }
        ptr++;
    }
    fflush(stdout);
    abort();
}

void CrashHandler::registerCrashHandler() {
    struct sigaction act;
    act.sa_handler = (void (*)(int)) handleSignal;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGSEGV, &act, 0);
    sigaction(SIGABRT, &act, 0);
}