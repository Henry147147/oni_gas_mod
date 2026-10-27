/* Runs the mod's game-contract check inside the game's own Mono runtime,
 * without Unity (docs/game-update-resilience.md#verification). It loads the
 * licensed installation's libmonobdwgc-2.0.so and Managed assemblies in place
 * and calls MixedAtmospheres.Oni.GameContractCheck.SelfCheck with the folder's
 * game-contract.json (Harmony's PatchAll itself needs the Unity player):
 * reflection only; no game code, Harmony patch or native simulator runs.
 *
 *   python3 tools/game_mono_contract_check.py --game GAME_ROOT --mod MOD_DIRECTORY
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void* (*init_fn)(const char*, const char*);
typedef void (*dirs_fn)(const char*, const char*);
typedef void (*path_fn)(const char*);
typedef void* (*open_fn)(void*, const char*);
typedef void* (*image_fn)(void*);
typedef void* (*class_fn)(void*, const char*, const char*);
typedef void* (*method_fn)(void*, const char*, int);
typedef void* (*string_fn)(void*, const char*);
typedef void* (*invoke_fn)(void*, void*, void**, void**);
typedef char* (*utf8_fn)(void*);
typedef void* (*tostring_fn)(void*, void**);
typedef void (*config_fn)(const char*);

int main(int argc, char** argv) {
    if (argc != 6) { fprintf(stderr, "usage: %s GAME_ROOT MOD_DIRECTORY ASSEMBLY_DIRECTORY MANAGED_DIRECTORY PLATFORM\n", argv[0]); return 2; }
    char library[4096], managed[4096], etc[4096], search[8192], assembly[4096];
    snprintf(library, sizeof library, "%s/OxygenNotIncluded_Data/MonoBleedingEdge/x86_64/libmonobdwgc-2.0.so", argv[1]);
    snprintf(managed, sizeof managed, "%s", argv[4]); /* the game assemblies to check */
    snprintf(etc, sizeof etc, "%s/OxygenNotIncluded_Data/MonoBleedingEdge/etc", argv[1]);
    snprintf(search, sizeof search, "%s:%s", managed, argv[2]);
    snprintf(assembly, sizeof assembly, "%s/MixedAtmospheres.Oni.dll", argv[2]);
    void* mono = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (!mono) { fprintf(stderr, "Cannot load the game's Mono: %s\n", dlerror()); return 2; }
#define BIND(type, name) type name = (type)dlsym(mono, #name); if (!name) { fprintf(stderr, "Missing %s\n", #name); return 2; }
    BIND(dirs_fn, mono_set_dirs) BIND(path_fn, mono_set_assemblies_path) BIND(init_fn, mono_jit_init_version)
    BIND(open_fn, mono_domain_assembly_open) BIND(image_fn, mono_assembly_get_image) BIND(class_fn, mono_class_from_name)
    BIND(method_fn, mono_class_get_method_from_name) BIND(string_fn, mono_string_new) BIND(invoke_fn, mono_runtime_invoke)
    BIND(utf8_fn, mono_string_to_utf8) BIND(tostring_fn, mono_object_to_string) BIND(config_fn, mono_config_parse)
    /* The runtime config finds libmono-native.so in ASSEMBLY_DIRECTORY/../lib;
     * tools/game_mono_contract_check.py lays that out. */
    mono_set_dirs(argv[3], etc);
    mono_config_parse(NULL); /* the game's dllmap: System.Native -> libmono-native.so */
    mono_set_assemblies_path(search);
    void* domain = mono_jit_init_version("contract", "v4.0.30319");
    void* opened = domain ? mono_domain_assembly_open(domain, assembly) : NULL;
    if (!opened) { fprintf(stderr, "Cannot open %s\n", assembly); return 2; }
    void* check = mono_class_from_name(mono_assembly_get_image(opened), "MixedAtmospheres.Oni", "GameContractCheck");
    void* method = check ? mono_class_get_method_from_name(check, "SelfCheck", 2) : NULL;
    if (!method) { fprintf(stderr, "GameContractCheck.SelfCheck is missing\n"); return 2; }
    char contract_path[4096];
    snprintf(contract_path, sizeof contract_path, "%s/game-contract.json", argv[2]);
    FILE* file = fopen(contract_path, "rb");
    if (!file) { fprintf(stderr, "Cannot read %s\n", contract_path); return 2; }
    fseek(file, 0, SEEK_END); long size = ftell(file); fseek(file, 0, SEEK_SET);
    char* text_in = malloc((size_t)size + 1);
    if (!text_in || fread(text_in, 1, (size_t)size, file) != (size_t)size) { fprintf(stderr, "Cannot read %s\n", contract_path); return 2; }
    text_in[size] = 0; fclose(file);
    void* arguments[2] = { mono_string_new(domain, text_in), mono_string_new(domain, argv[5]) };
    void* exception = NULL;
    void* result = mono_runtime_invoke(method, NULL, arguments, &exception);
    if (exception) { void* inner = NULL; fprintf(stderr, "%s\n", mono_string_to_utf8(mono_object_to_string(exception, &inner))); return 1; }
    const char* text = mono_string_to_utf8(result);
    puts(text);
    return strstr(text, ", 0 differences") ? 0 : 1;
}
