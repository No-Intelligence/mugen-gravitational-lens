#ifndef MUGEN_MOD_H
#define MUGEN_MOD_H
#include <stdint.h>
#include <wchar.h>

#ifdef _WIN32
#define MML_CALL __cdecl
#define MML_EXPORT __declspec(dllexport)
#else
#define MML_CALL
#define MML_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define MML_ABI_VERSION 1u
#define MML_PHASE_BEFORE_ENGINE_START 1u

/* ABI v1: Windows x86, default packing (8), C calling convention.
   All pointers and strings supplied by the host live until process exit. */
typedef struct MugenModHostV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t phase;
    void *engine_base;
    const wchar_t *game_directory;
    const wchar_t *mod_directory;
    const char *mod_id;
    /* Log and find_export are callable from mod threads; log text is UTF-8.
       find_export exposes only successfully initialized earlier mods. */
    void (MML_CALL *log)(const char *mod_id, const char *message_utf8);
    void *(MML_CALL *find_export)(const char *mod_id, const char *symbol);
} MugenModHostV1;

typedef uint32_t (MML_CALL *MugenModGetAbiVersionFn)(void);
typedef int32_t (MML_CALL *MugenModInitFn)(const MugenModHostV1 *host);

/* Each mod.dll exports these two exact, case-sensitive names.
   Init returns 0 on success. Do initialization here, not in DllMain. */
MML_EXPORT uint32_t MML_CALL MugenMod_GetAbiVersion(void);
MML_EXPORT int32_t MML_CALL MugenMod_Init(const MugenModHostV1 *host);
#ifdef __cplusplus
}
#endif
#endif
