#include <mcpelauncher/minecraft_utils.h>
#include <mcpelauncher/patch_utils.h>
#include <mcpelauncher/hybris_utils.h>
#include <mcpelauncher/fmod_utils.h>
#include <mcpelauncher/hook.h>
#include <mcpelauncher/path_helper.h>
#include <mcpelauncher/minecraft_version.h>
#include <minecraft/imported/android_symbols.h>
#include <minecraft/imported/egl_symbols.h>
#include <minecraft/imported/libm_symbols.h>
#include <minecraft/imported/fmod_symbols.h>
#include <minecraft/imported/glesv2_symbols.h>
#include <minecraft/imported/libz_symbols.h>
#include <log.h>
#include <FileUtil.h>
#include <mcpelauncher/linker.h>
#include <libc_shim.h>
#include <stdexcept>
#include <cstring>
#if defined(__APPLE__) && defined(__aarch64__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

void MinecraftUtils::workaroundLocaleBug() {
    setenv("LC_ALL", "C", 1);  // HACK: Force set locale to one recognized by MCPE so that the outdated C++ standard library MCPE uses doesn't fail to find one
}

static bool ReadEnvFlag(const char* name, bool def = false) {
    auto val = getenv(name);
    if(!val) {
        return def;
    }
    std::string sval = val;
    return sval == "true" || sval == "1" || sval == "on";
}

std::unordered_map<std::string, void*> MinecraftUtils::getLibCSymbols() {
    std::unordered_map<std::string, void*> syms;
    for(auto const& s : shim::get_shimmed_symbols())
        syms[s.name] = s.value;
    return syms;
}

void* MinecraftUtils::loadLibM() {
#ifdef __APPLE__
    void* libmLib = HybrisUtils::loadLibraryOS("libm.so", "libm.dylib", libm_symbols, std::unordered_map<std::string, void*>{{std::string("sincos"), (void*)__sincos}, {std::string("sincosf"), (void*)__sincosf}});
#elif defined(__FreeBSD__)
    void* libmLib = HybrisUtils::loadLibraryOS("libm.so", "libm.so", libm_symbols);
#else
    void* libmLib = HybrisUtils::loadLibraryOS("libm.so", "libm.so.6", libm_symbols);
#endif
    if(libmLib == nullptr)
        throw std::runtime_error("Failed to load libm");
    return libmLib;
}

void* MinecraftUtils::loadFMod() {
    void* fmodLib = HybrisUtils::loadLibraryOS("libfmod.so", PathHelper::findDataFile(std::string("lib/native/") + getLibraryAbi() +
#ifdef __APPLE__
#if defined(__i386__)
                                                                                      // Minecraft releases linked against libc++-shared have to use a newer version of libfmod
                                                                                      // Throwing here allows using pulseaudio if available / starting the game without sound
                                                                                      (linker::dlopen("libc++_shared.so", 0) ? throw std::runtime_error("Fmod removed i386 support, after deprecation by Apple") : "/libfmod.dylib")
#else
                                                                                      "/libfmod.dylib"
#endif
#else
#ifdef __LP64__
                                                                                      "/libfmod.so.12.0"
#else
                                                                                      // Minecraft releases linked against libc++-shared have to use a newer version of libfmod
                                                                                      (linker::dlopen("libc++_shared.so", 0) ? "/libfmod.so.12.0" : "/libfmod.so.10.20")
#endif
#endif
                                                                                          ),
                                               fmod_symbols);
    if(fmodLib == nullptr)
        throw std::runtime_error("Failed to load fmod");
    return fmodLib;
}

void MinecraftUtils::stubFMod() {
    HybrisUtils::stubSymbols("libfmod.so", fmod_symbols, (void*)(void* (*)())[]() {
        Log::warn("Launcher", "FMod stub called");
        return (void*) nullptr; });
}

void MinecraftUtils::setupHybris() {
    HybrisUtils::loadLibraryOS("libz.so",
#ifdef __APPLE__
                               "libz.dylib"
#elif defined(__FreeBSD__)
                               "libz.so"
#else
                               "libz.so.1"
#endif
                               ,
                               libz_symbols);
    HybrisUtils::hookAndroidLog();
    setupApi();
    linker::load_library("libOpenSLES.so", {});
    linker::load_library("libGLESv1_CM.so", {});

    linker::load_library("libstdc++.so", {});
    linker::load_library("libz.so", {});  // needed for <0.17
}

std::unordered_map<std::string, void*> MinecraftUtils::getApi() {
    std::unordered_map<std::string, void*> syms;
    // Deprecated use android liblog
#if !(defined(__APPLE__) && defined(__aarch64__))
    syms["mcpelauncher_log"] = (void*)Log::log;
    syms["mcpelauncher_vlog"] = (void*)Log::vlog;
#endif

    syms["mcpelauncher_preinithook2"] = (void*)(void (*)(const char*, void*, void*, void (*)(void*, void*)))[](const char* name, void* sym, void* user, void (*callback)(void*, void*)) {
        preinitHooks[name] = {sym, user, callback};
    };
    syms["mcpelauncher_preinithook"] = (void*)(void (*)(const char*, void*, void**))[](const char* name, void* sym, void** orig) {
        auto&& def = [](void* user, void* orig) {
            *(void**)user = orig;
        };
        preinitHooks[name] = {sym, orig, orig ? def : nullptr};
    };

    syms["mcpelauncher_hook"] = (void*)(void* (*)(void*, void*, void**))[](void* sym, void* hook, void** orig) {
        Dl_info i;
        if(!linker::dladdr(sym, &i)) {
            Log::error("Hook", "Failed to resolve hook for symbol %lx", (long unsigned)sym);
            return (void*)nullptr;
        }
        void* handle = linker::dlopen(i.dli_fname, 0);
        std::string tName = HookManager::translateConstructorName(i.dli_sname);
        auto ret = HookManager::instance.createHook(handle, tName.empty() ? i.dli_sname : tName.c_str(), hook, orig);
        linker::dlclose(handle);
        HookManager::instance.applyHooks();
        return (void*)ret;
    };

    syms["mcpelauncher_hook2"] = (void*)(void* (*)(void*, const char*, void*, void**))
        [](void* lib, const char* sym, void* hook, void** orig) {
        return (void*)HookManager::instance.createHook(lib, sym, hook, orig);
    };
    syms["mcpelauncher_hook2_add_library"] = (void*)(void (*)(void*))[](void* lib) {
        HookManager::instance.addLibrary(lib);
    };
    syms["mcpelauncher_hook2_remove_library"] = (void*)(void (*)(void*))[](void* lib) {
        HookManager::instance.removeLibrary(lib);
    };
    syms["mcpelauncher_hook2_delete"] = (void*)(void (*)(void*))[](void* hook) {
        HookManager::instance.deleteHook((HookManager::HookInstance*)hook);
    };
    syms["mcpelauncher_hook2_apply"] = (void*)(void (*)())[]() {
        HookManager::instance.applyHooks();
    };
#if defined(__APPLE__) && defined(__aarch64__)
    syms["mcpelauncher_patch"] = (void*)+[](void* address, void* data, size_t size) -> void* {
        pthread_jit_write_protect_np(0);
        auto ret = memcpy(address, data, size);
        sys_icache_invalidate(address, size);
        pthread_jit_write_protect_np(1);
        return ret;
    };
#else
    syms["mcpelauncher_patch"] = (void*)+[](void* address, void* data, size_t size) -> void* {
        return memcpy(address, data, size);
    };
#endif
    syms["mcpelauncher_host_dlopen"] = (void*)dlopen;
    syms["mcpelauncher_host_dlsym"] = (void*)dlsym;
    syms["mcpelauncher_host_dlclose"] = (void*)dlclose;
    syms["mcpelauncher_relocate"] = (void*)+[](void* handle, const char* name, void* hook) {
        linker::relocate(handle, {{name, hook}});
    };
    struct hook_entry {
        const char* name;
        void* hook;
    };
    syms["mcpelauncher_relocate2"] = (void*)+[](void* handle, size_t count, hook_entry* entries) {
        std::unordered_map<std::string, void*> ventries;
        for(size_t i = 0; i < count; i++) {
            ventries[entries[i].name] = entries[i].hook;
        }
        linker::relocate(handle, ventries);
    };
    syms["mcpelauncher_load_library"] = (void*)+[](const char* name, size_t count, hook_entry* entries) {
        std::unordered_map<std::string, void*> ventries;
        for(size_t i = 0; i < count; i++) {
            ventries[entries[i].name] = entries[i].hook;
        }
        linker::load_library(name, ventries);
    };
    syms["mcpelauncher_unload_library"] = (void*)linker::unload_library;
    syms["mcpelauncher_dlclose_unlocked"] = (void*)linker::dlclose_unlocked;

    return syms;
}

void MinecraftUtils::setupApi() {
    linker::load_library("libmcpelauncher_mod.so", getApi());
}

std::unordered_map<std::string, MinecraftUtils::HookEntry> MinecraftUtils::preinitHooks;

void* MinecraftUtils::loadMinecraftLib(void* showMousePointerCallback, void* hideMousePointerCallback, void* fullscreenCallback, void* closeCallback, std::vector<mcpelauncher_hook_t> hooks) {
    auto libcxx = linker::dlopen("libc++_shared.so", 0);
    if(!libcxx) {
        Log::error("MinecraftUtils", "Failed to load libc++_shared: %s", linker::dlerror());
    }
    // loading libfmod standalone depends on these symbols, libminecraftpe.so changes the loading automatically
    auto libstdcxx = linker::dlopen("libstdc++.so", 0);
    if(libcxx) {
        auto __cxa_pure_virtual = linker::dlsym(libcxx, "__cxa_pure_virtual");
        auto __cxa_guard_acquire = linker::dlsym(libcxx, "__cxa_guard_acquire");
        auto __cxa_guard_release = linker::dlsym(libcxx, "__cxa_guard_release");

        if(__cxa_pure_virtual && __cxa_guard_acquire && __cxa_guard_release) {
            linker::relocate(libstdcxx, {{"__cxa_pure_virtual", __cxa_pure_virtual}, {"__cxa_guard_acquire", __cxa_guard_acquire}, {"__cxa_guard_release", __cxa_guard_release}});
        }
    }
    android_dlextinfo extinfo;
#ifdef __arm__
    // Workaround for v8 allocator crash Minecraft 1.16.100+ on a RaspberryPi2 running raspbian
    // Shadow some new overrides with host allocator fixes the crash
    // Seems to be unnecessary on a RaspberryPi4 running ubuntu arm64
    hooks.emplace_back(mcpelauncher_hook_t{"_Znaj", (void*)((void* (*)(std::size_t)) & ::operator new[])});
    hooks.emplace_back(mcpelauncher_hook_t{"_Znwj", (void*)((void* (*)(std::size_t)) & ::operator new)});
    hooks.emplace_back(mcpelauncher_hook_t{"_ZnwjSt11align_val_t", (void*)((void* (*)(std::size_t, std::align_val_t)) & ::operator new[])});
    // The Openssl cpuid setup seems to not work correctly and allways crashs with "invalid instruction" Minecraft 1.16.10 (beta 1.16.0.66) or lower
    // Shadowing it, avoids allways defining OPENSSL_armcap=0
    hooks.emplace_back(mcpelauncher_hook_t{"OPENSSL_cpuid_setup", (void*)+[]() -> void {}});
#endif

    for(auto&& e : preinitHooks) {
        hooks.emplace_back(mcpelauncher_hook_t{e.first.data(), e.second.value});
    }

    // Minecraft 1.16.210+ removes the symbols previously used to patch it via vtables, so use hooks instead if supplied
    if(showMousePointerCallback && hideMousePointerCallback) {
        hooks.emplace_back(mcpelauncher_hook_t{"_ZN11AppPlatform16showMousePointerEv", showMousePointerCallback});
        hooks.emplace_back(mcpelauncher_hook_t{"_ZN11AppPlatform16hideMousePointerEv", hideMousePointerCallback});
    }
    if(fullscreenCallback) {
        hooks.emplace_back(mcpelauncher_hook_t{"_ZN11AppPlatform17setFullscreenModeE14FullscreenMode", fullscreenCallback});
    }

    if(closeCallback) {
        hooks.emplace_back(mcpelauncher_hook_t{"GameActivity_finish", closeCallback});
    }

    static void* fmod = nullptr;
    // Temporary feature flag to disable native fmod patching
    if(!fmod && ReadEnvFlag("MCPELAUNCHER_PATCH_FMOD", true)) {
        fmod = linker::dlopen("libfmod.so", 0);
    }
    if(fmod) {
        if(linker::get_library_base(fmod)) {
            if(FmodUtils::setup(fmod)) {
                hooks.emplace_back(mcpelauncher_hook_t{"_ZN4FMOD6System4initEijPv", reinterpret_cast<void*>(&FmodUtils::initHook)});
                hooks.emplace_back(mcpelauncher_hook_t{"_ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE", (void*)+[]() {
                                                           // stub to make the game use aaudio
                                                       }});
            }
        } else {
            linker::dlclose(fmod);
            fmod = nullptr;
        }
    }
    auto libc = linker::dlopen("libc.so", 0);
    // Detect Android Integrity Protection
    void* pairipcore = linker::dlopen("libpairipcore.so", 0);
    if(!pairipcore) {
        Log::error("MinecraftUtils", "Failed to load libpairipcore: %s", linker::dlerror());
    } else {
        Log::info("MinecraftUtils", "Loaded libpairipcore");
    }

    // webrtc shortcut
    auto bgetifaddrs = linker::dlsym(libc, "getifaddrs");
    if(bgetifaddrs) {
        hooks.emplace_back(mcpelauncher_hook_t{"_ZN3rtc10getifaddrsEPP7ifaddrs", bgetifaddrs});
    }
    auto bfreeifaddrs = linker::dlsym(libc, "freeifaddrs");
    if(bfreeifaddrs) {
        hooks.emplace_back(mcpelauncher_hook_t{"_ZN3rtc11freeifaddrsEP7ifaddrs", bfreeifaddrs});
    }
    if(ReadEnvFlag("MCPELAUNCHER_DISABLE_TELEMETRY", false)) {
        hooks.emplace_back(mcpelauncher_hook_t{"_ZN9Microsoft12Applications6Events19TelemetrySystemBase5startEv", (void*)+[]() {
            Log::error("MinecraftUtils", "TelemetrySystemBase::start");
        }});
    } else if(pairipcore) {
        auto sqlite3 = linker::dlopen(PathHelper::findDataFile("lib/" + std::string(PathHelper::getAbiDir()) + "/libsqliteX.so").c_str(), 0);
        const char* sqlite3_symbols[] = {
            "sqlite3_release_memory",
            "sqlite3_vtab_in_next",
            "sqlite3_autovacuum_pages",
            "sqlite3_column_text",
            "sqlite3_set_clientdata",
            "sqlite3_column_name16",
            "sqlite3_drop_modules",
            "sqlite3_close_v2",
            "sqlite3_config",
            "sqlite3_vtab_nochange",
            "sqlite3_blob_open",
            "sqlite3_db_mutex",
            "sqlite3_open_v2",
            "sqlite3_create_module",
            "sqlite3_blob_read",
            "sqlite3_open",
            "sqlite3_hard_heap_limit64",
            "sqlite3_create_filename",
            "sqlite3_status64",
            "sqlite3_file_control",
            "sqlite3_value_subtype",
            "sqlite3_test_control",
            "sqlite3_backup_init",
            "sqlite3_create_function",
            "sqlite3_open16",
            "sqlite3_finalize",
            "sqlite3_result_text",
            "sqlite3_column_blob",
            "sqlite3_value_pointer",
            "sqlite3_bind_blob64",
            "sqlite3_blob_bytes",
            "sqlite3_os_end",
            "sqlite3_result_error",
            "sqlite3_blob_write",
            "sqlite3_randomness",
            "sqlite3_wal_checkpoint_v2",
            "sqlite3_result_error_toobig",
            "sqlite3_last_insert_rowid",
            "sqlite3_realloc64",
            "sqlite3_stmt_readonly",
            "sqlite3_os_init",
            "sqlite3_result_text64",
            "sqlite3_context_db_handle",
            "sqlite3_close",
            "sqlite3_value_type",
            "sqlite3_value_nochange",
            "sqlite3_get_auxdata",
            "sqlite3_bind_int",
            "sqlite3_create_module_v2",
            "sqlite3_busy_timeout",
            "sqlite3_memory_used",
            "sqlite3_deserialize",
            "sqlite3_column_value",
            "sqlite3_result_error_nomem",
            "sqlite3_changes64",
            "sqlite3_str_vappendf",
            "sqlite3_declare_vtab",
            "sqlite3_result_blob",
            "sqlite3_vtab_distinct",
            "sqlite3_str_append",
            "sqlite3_profile",
            "sqlite3_bind_text",
            "sqlite3_collation_needed",
            "sqlite3_vtab_in_first",
            "sqlite3_result_int64",
            "sqlite3_errmsg16",
            "sqlite3_errmsg",
            "sqlite3_user_data",
            "sqlite3_uri_parameter",
            "sqlite3_mutex_try",
            "sqlite3_result_int",
            "sqlite3_free_table",
            "sqlite3_complete",
            "sqlite3_commit_hook",
            "sqlite3_reset",
            "sqlite3_vfs_register",
            "sqlite3_clear_bindings",
            "sqlite3_prepare_v2",
            "sqlite3_next_stmt",
            "sqlite3_prepare_v3",
            "sqlite3_malloc64",
            "sqlite3_bind_blob",
            "sqlite3_shutdown",
            "sqlite3_strglob",
            "sqlite3_filename_database",
            "sqlite3_column_decltype",
            "sqlite3_sql",
            "sqlite3_get_clientdata",
            "sqlite3_db_status",
            "sqlite3_progress_handler",
            "sqlite3_rollback_hook",
            "sqlite3_value_frombind",
            "sqlite3_str_appendf",
            "sqlite3_result_zeroblob64",
            "sqlite3_msize",
            "sqlite3_column_int",
            "sqlite3_set_authorizer",
            "sqlite3_compileoption_get",
            "sqlite3_bind_text64",
            "sqlite3_filename_journal",
            "sqlite3_version",
            "sqlite3_value_text16be",
            "sqlite3_result_text16",
            "sqlite3_stmt_busy",
            "sqlite3_enable_load_extension",
            "sqlite3_keyword_check",
            "sqlite3_snprintf",
            "sqlite3_prepare16_v2",
            "sqlite3_keyword_count",
            "sqlite3_interrupt",
            "sqlite3_prepare16_v3",
            "sqlite3_vfs_find",
            "sqlite3_backup_remaining",
            "sqlite3_create_collation",
            "sqlite3_create_function_v2",
            "sqlite3_bind_int64",
            "sqlite3_initialize",
            "sqlite3_stricmp",
            "sqlite3_db_cacheflush",
            "sqlite3_update_hook",
            "sqlite3_strlike",
            "sqlite3_error_offset",
            "sqlite3_collation_needed16",
            "sqlite3_uri_key",
            "sqlite3_backup_step",
            "sqlite3_result_text16be",
            "sqlite3_extended_errcode",
            "sqlite3_blob_close",
            "sqlite3_column_decltype16",
            "sqlite3_db_release_memory",
            "sqlite3_sourceid",
            "sqlite3_aggregate_count",
            "sqlite3_reset_auto_extension",
            "sqlite3_thread_cleanup",
            "sqlite3_busy_handler",
            "sqlite3_result_double",
            "sqlite3_value_bytes16",
            "sqlite3_status",
            "sqlite3_mutex_free",
            "sqlite3_extended_result_codes",
            "sqlite3_uri_int64",
            "sqlite3_expanded_sql",
            "sqlite3_complete16",
            "sqlite3_column_text16",
            "sqlite3_column_bytes16",
            "sqlite3_memory_highwater",
            "sqlite3_str_reset",
            "sqlite3_set_auxdata",
            "sqlite3_rtree_query_callback",
            "sqlite3_bind_parameter_index",
            "sqlite3_mutex_alloc",
            "sqlite3_data_directory",
            "sqlite3_column_type",
            "sqlite3_blob_reopen",
            "sqlite3_value_text16",
            "sqlite3_setlk_timeout",
            "sqlite3_db_readonly",
            "sqlite3_exec",
            "sqlite3_auto_extension",
            "sqlite3_libversion",
            "sqlite3_str_appendchar",
            "sqlite3_vmprintf",
            "sqlite3_trace_v2",
            "sqlite3_global_recover",
            "sqlite3_bind_text16",
            "sqlite3_str_appendall",
            "sqlite3_value_text",
            "sqlite3_vtab_collation",
            "sqlite3_is_interrupted",
            "sqlite3_str_length",
            "sqlite3_txn_state",
            "sqlite3_column_bytes",
            "sqlite3_str_value",
            "sqlite3_column_double",
            "sqlite3_changes",
            "sqlite3_str_finish",
            "sqlite3_result_null",
            "sqlite3_get_autocommit",
            "sqlite3_sleep",
            "sqlite3_column_int64",
            "sqlite3_realloc",
            "sqlite3_transfer_bindings",
            "sqlite3_cancel_auto_extension",
            "sqlite3_wal_hook",
            "sqlite3_errcode",
            "sqlite3_bind_parameter_name",
            "sqlite3_column_count",
            "sqlite3_backup_finish",
            "sqlite3_mutex_leave",
            "sqlite3_value_double",
            "sqlite3_threadsafe",
            "sqlite3_create_collation_v2",
            "sqlite3_create_collation16",
            "sqlite3_mutex_enter",
            "sqlite3_stmt_explain",
            "sqlite3_expired",
            "sqlite3_overload_function",
            "sqlite3_log",
            "sqlite3_vsnprintf",
            "sqlite3_data_count",
            "sqlite3_errstr",
            "sqlite3_free_filename",
            "sqlite3_compileoption_used",
            "sqlite3_value_blob",
            "sqlite3_total_changes",
            "sqlite3_wal_autocheckpoint",
            "sqlite3_memory_alarm",
            "sqlite3_value_dup",
            "sqlite3_total_changes64",
            "sqlite3_bind_double",
            "sqlite3_get_table",
            "sqlite3_strnicmp",
            "sqlite3_serialize",
            "sqlite3_free",
            "sqlite3_result_subtype",
            "sqlite3_result_value",
            "sqlite3_step",
            "sqlite3_stmt_isexplain",
            "sqlite3_bind_parameter_count",
            "sqlite3_vtab_rhs_value",
            "sqlite3_db_filename",
            "sqlite3_trace",
            "sqlite3_bind_null",
            "sqlite3_result_error16",
            "sqlite3_result_pointer",
            "sqlite3_set_last_insert_rowid",
            "sqlite3_value_numeric_type",
            "sqlite3_result_error_code",
            "sqlite3_temp_directory",
            "sqlite3_system_errno",
            "sqlite3_aggregate_context",
            "sqlite3_wal_checkpoint",
            "sqlite3_str_errcode",
            "sqlite3_vfs_unregister",
            "sqlite3_prepare",
            "sqlite3_enable_shared_cache",
            "sqlite3_create_window_function",
            "sqlite3_load_extension",
            "sqlite3_keyword_name",
            "sqlite3_prepare16",
            "sqlite3_db_handle",
            "sqlite3_backup_pagecount",
            "sqlite3_value_encoding",
            "sqlite3_db_config",
            "sqlite3_libversion_number",
            "sqlite3_column_name",
            "sqlite3_soft_heap_limit",
            "sqlite3_value_int",
            "sqlite3_limit",
            "sqlite3_uri_boolean",
            "sqlite3_mprintf",
            "sqlite3_vtab_on_conflict",
            "sqlite3_vtab_config",
            "sqlite3_table_column_metadata",
            "sqlite3_result_blob64",
            "sqlite3_create_function16",
            "sqlite3_value_text16le",
            "sqlite3_rtree_geometry_callback",
            "sqlite3_database_file_object",
            "sqlite3_value_bytes",
            "sqlite3_stmt_status",
            "sqlite3_vtab_in",
            "sqlite3_result_zeroblob",
            "sqlite3_value_free",
            "sqlite3_db_name",
            "sqlite3_malloc",
            "sqlite3_soft_heap_limit64",
            "sqlite3_value_int64",
            "sqlite3_bind_value",
            "sqlite3_str_new",
            "sqlite3_filename_wal",
            "sqlite3_result_text16le",
            "sqlite3_bind_zeroblob",
            "sqlite3_bind_pointer",
            "sqlite3_bind_zeroblob64"};
        
        if(sqlite3) {
            for(auto&& sym : sqlite3_symbols) {
                auto addr = linker::dlsym(sqlite3, sym);
                if(addr) {
                    hooks.emplace_back(mcpelauncher_hook_t{sym, addr});
                } else {
                    Log::error("MinecraftUtils", "Failed to find symbol %s in sqlite3", sym);
                }
            }
        } else {
            Log::error("MinecraftUtils", "Failed to load sqlite3");
        }
    }

    hooks.emplace_back(mcpelauncher_hook_t{nullptr, nullptr});
    extinfo.flags = ANDROID_DLEXT_MCPELAUNCHER_HOOKS;
    extinfo.mcpelauncher_hooks = hooks.data();
    void* handle = linker::dlopen_ext("libminecraftpe.so", 0, &extinfo);
    if(libc) {
        linker::dlclose(libc);
    }
    if(libcxx) {
        linker::dlclose(libcxx);
    }
    if(libstdcxx) {
        linker::dlclose(libstdcxx);
    }
    if(pairipcore) {
        linker::dlclose(pairipcore);
    }
    if(handle == nullptr) {
        Log::error("MinecraftUtils", "Failed to load Minecraft: %s", linker::dlerror());
    } else {
        if(fmod) {
            // We cannot load this again, so make it static and unload it once
            linker::dlclose(fmod);
        }
        for(auto&& h : hooks) {
            if(h.name) {
                printf("Found hook: %s @ %p\n", h.name, linker::dlsym(handle, h.name));
                if(auto&& res = preinitHooks.find(h.name); res != preinitHooks.end() && res->second.callback != nullptr) {
                    printf("with value: %p\n", h.value);
                    res->second.callback(res->second.user, h.value);
                }
            }
        }
        HookManager::instance.addLibrary(handle);
    }
    return handle;
}
const char* MinecraftUtils::getLibraryAbi() {
    return PathHelper::getAbiDir();
}

size_t MinecraftUtils::getLibraryBase(void* handle) {
    return linker::get_library_base(handle);
}

void MinecraftUtils::setupGLES2Symbols(void* (*resolver)(const char*)) {
    int i = 0;
    std::unordered_map<std::string, void*> syms;
    while(true) {
        const char* sym = glesv2_symbols[i];
        if(sym == nullptr)
            break;
        syms[sym] = resolver(sym);
        i++;
    }
    linker::load_library("libGLESv2.so", syms);
}
