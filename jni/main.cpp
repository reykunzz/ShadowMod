// ShadowMod v1.1 - realistic shadows for GTA SA Android v2.00 (armeabi-v7a)
// Setting dibaca dari ShadowMod.ini (dibuat otomatis kalau belum ada).
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>

MYMOD(net.kamu.shadowmod, ShadowMod, 1.1, KamuNama)

// ============================ CONFIG ============================
struct Config
{
    bool  enabled       = true;
    int   resolution    = 512;    // 128..1024, dibulatkan ke pangkat dua
    float strengthScale = 1.30f;  // 0.0..3.0  (1.0 = bawaan game)
    float pedDistance   = 40.0f;  // 1..200
};
static Config cfg;

static const char* kDefaultIni =
"; ShadowMod config - GTA SA Android 2.00\n"
"; Ubah nilai lalu restart game.\n"
"[Shadow]\n"
"Enabled=1\n"
"; Resolusi texture shadow real-time: 256 / 512 / 1024 (makin besar makin berat)\n"
"Resolution=512\n"
"; 1.0 = bawaan game, lebih dari 1.0 = bayangan lebih gelap\n"
"StrengthScale=1.30\n"
"; Jarak maksimum bayangan ped\n"
"PedShadowDistance=40.0\n";

static int RoundPow2(int v)
{
    int p = 128;
    while (p < v && p < 1024) p <<= 1;
    return p;
}

static void Trim(char* s)
{
    char* a = s;
    while (*a && isspace((unsigned char)*a)) ++a;
    memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void ParseLine(char* line)
{
    Trim(line);
    if (!*line || *line == ';' || *line == '#' || *line == '[') return;
    char* eq = strchr(line, '=');
    if (!eq) return;
    *eq = 0;
    char* key = line;
    char* val = eq + 1;
    Trim(key); Trim(val);

    if      (!strcasecmp(key, "Enabled"))           cfg.enabled       = atoi(val) != 0;
    else if (!strcasecmp(key, "Resolution"))        cfg.resolution    = RoundPow2(atoi(val));
    else if (!strcasecmp(key, "StrengthScale"))     cfg.strengthScale = std::min(3.0f,   std::max(0.0f, (float)atof(val)));
    else if (!strcasecmp(key, "PedShadowDistance")) cfg.pedDistance   = std::min(200.0f, std::max(1.0f, (float)atof(val)));
}

static void LoadConfig()
{
    char path[512];
    const char* dir = aml->GetConfigPath();
    if (!dir || !*dir) dir = "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/configs/";
    size_t n = strlen(dir);
    snprintf(path, sizeof(path), "%s%sShadowMod.ini", dir, (dir[n - 1] == '/') ? "" : "/");
    logger->Info("Config: %s", path);

    FILE* f = fopen(path, "r");
    if (!f)
    {
        f = fopen(path, "w");           // buat file default
        if (f) { fputs(kDefaultIni, f); fclose(f); logger->Info("ShadowMod.ini default dibuat"); }
        else   { logger->Error("Tidak bisa membuat ShadowMod.ini, pakai nilai bawaan"); }
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) ParseLine(line);
    fclose(f);
    logger->Info("Config: enabled=%d res=%d strength=%.2f pedDist=%.1f",
                 cfg.enabled, cfg.resolution, cfg.strengthScale, cfg.pedDistance);
}

// ============================ HOOKS ============================
static void* hGTASA = nullptr;

static uint8_t* pShadowStrength      = nullptr; // CTimeCycle::m_nShadowStrength      [8*23]
static uint8_t* pLightShadowStrength = nullptr; // CTimeCycle::m_nLightShadowStrength [8*23]
static uint8_t* pPoleShadowStrength  = nullptr; // CTimeCycle::m_nPoleShadowStrength  [8*23]
static float*   pPedShadowDist       = nullptr; // MAX_DISTANCE_PED_SHADOWS
static float*   pPedShadowDistSqr    = nullptr; // MAX_DISTANCE_PED_SHADOWS_SQR

static const int kTCSize = 8 * 23;
static uint8_t origShadow[kTCSize], origLight[kTCSize], origPole[kTCSize];
static bool    snapshotTaken = false;

static void ScaleArray(uint8_t* dst, const uint8_t* src, float scale)
{
    for (int i = 0; i < kTCSize; ++i)
    {
        int v = (int)(src[i] * scale + 0.5f);
        dst[i] = (uint8_t)std::min(255, std::max(0, v));
    }
}

static void* (*Create_orig)(void* self, int size) = nullptr;
static void* Create_hook(void* self, int size)
{
    int newSize = std::max(size, cfg.resolution);
    logger->Info("CShadowCamera::Create size %d -> %d", size, newSize);
    return Create_orig(self, newSize);
}

static void (*Process_orig)() = nullptr;
static void Process_hook()
{
    if (!snapshotTaken && pShadowStrength && pLightShadowStrength && pPoleShadowStrength)
    {
        memcpy(origShadow, pShadowStrength,      kTCSize);
        memcpy(origLight,  pLightShadowStrength, kTCSize);
        memcpy(origPole,   pPoleShadowStrength,  kTCSize);
        snapshotTaken = true;
        logger->Info("Snapshot timecycle shadow diambil");
    }
    if (snapshotTaken)
    {
        ScaleArray(pShadowStrength,      origShadow, cfg.strengthScale);
        ScaleArray(pLightShadowStrength, origLight,  cfg.strengthScale);
        ScaleArray(pPoleShadowStrength,  origPole,   cfg.strengthScale);
    }
    if (pPedShadowDist)    *pPedShadowDist    = cfg.pedDistance;
    if (pPedShadowDistSqr) *pPedShadowDistSqr = cfg.pedDistance * cfg.pedDistance;

    Process_orig();
}

static void* Sym(const char* name)
{
    void* p = dlsym(hGTASA, name);
    if (!p) logger->Error("Simbol TIDAK ketemu: %s", name);
    return p;
}

ON_MOD_PRE_LOAD()
{
    logger->SetTag("ShadowMod");
}

ON_MOD_LOAD()
{
    LoadConfig();
    if (!cfg.enabled) { logger->Info("Enabled=0, mod tidak dipasang"); return; }

    hGTASA = dlopen("libGTASA.so", RTLD_LAZY);
    if (!hGTASA) { logger->Error("dlopen libGTASA.so gagal"); return; }

    pShadowStrength      = (uint8_t*)Sym("_ZN10CTimeCycle17m_nShadowStrengthE");
    pLightShadowStrength = (uint8_t*)Sym("_ZN10CTimeCycle22m_nLightShadowStrengthE");
    pPoleShadowStrength  = (uint8_t*)Sym("_ZN10CTimeCycle21m_nPoleShadowStrengthE");
    pPedShadowDist       = (float*)  Sym("MAX_DISTANCE_PED_SHADOWS");
    pPedShadowDistSqr    = (float*)  Sym("MAX_DISTANCE_PED_SHADOWS_SQR");

    void* fCreate  = Sym("_ZN13CShadowCamera6CreateEi");
    void* fProcess = Sym("_ZN5CGame7ProcessEv");

    if (fCreate)  aml->Hook(fCreate,  (void*)Create_hook,  (void**)&Create_orig);
    if (fProcess) aml->Hook(fProcess, (void*)Process_hook, (void**)&Process_orig);

    logger->Info("ShadowMod loaded");
}
