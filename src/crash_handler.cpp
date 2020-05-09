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

#if defined(__x86_64__) && defined(__APPLE__)
void CrashHandler::handle_fs_fault(int sig, void *si, void *ucp) {
  ucontext_t *uc = (ucontext_t*)ucp;
  unsigned char *p = (unsigned char *)uc->uc_mcontext->__ss.__rip;
  if (p && *p == 0x64) {
    *p = 0x65;
  } else if (p && *p == 0x65) {
  } else {
    // Not a %fs fault, attach normal crash handler to sigsegv
    struct sigaction act;
    act.sa_handler = (void (*)(int)) handleSignal;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGSEGV, &act, 0);
  }
}
#endif

void CrashHandler::registerCrashHandler() {
    struct sigaction act;
    act.sa_handler = (void (*)(int)) handleSignal;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
#if defined(__x86_64__) && defined(__APPLE__)
    {
        struct sigaction act;
        act.sa_sigaction = (void (*)(int, __siginfo *, void *)) handle_fs_fault;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &act, 0);
    }
#else
    sigaction(SIGSEGV, &act, 0);
#endif
    sigaction(SIGABRT, &act, 0);
}