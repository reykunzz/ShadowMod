// ShadowMod v1.5 - realistic shadows for GTA SA Android v2.00 (armeabi-v7a)
// Tiap fitur bisa dinyalakan terpisah lewat ShadowMod.ini, dan langkahnya dicatat ke ShadowMod.log
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <algorithm>

MYMOD(net.kamu.shadowmod, ShadowMod, 1.5, KamuNama)

// ============================ CONFIG ============================
struct Config
{
    bool  enabled       = true;
    int   resolutionBoost = 0;    // 0 = mati. 1 = resolusi 2x (512->1024), 2 = maks (semua ke 1024)
    bool  forceAdvanced = false;  // paksa bayangan real-time menyala (MobileSettings+0xA8 = 2)
    int   boostFromPx   = 256;    // hanya kamera berukuran asli >= nilai ini yang dinaikkan
    int   boostToPx     = 512;    // ... dan <= nilai ini
    float strengthScale = 1.0f;   // 1.0 = mati. Lebih dari 1.0 = bayangan lebih gelap
    float pedDistance   = 0.0f;   // 0 = mati. Jarak max bayangan ped
};
static Config cfg;

static const char* kDefaultIni =
"; ShadowMod v1.3 - GTA SA Android 2.00\n"
"; Default: semua fitur MATI (mod tidak mengubah apa-apa).\n"
"; Nyalakan SATU per satu lalu tes, supaya ketahuan kalau ada yang bikin crash.\n"
"[Shadow]\n"
"Enabled=1\n"
"; Resolusi shadow real-time: 0=mati, 1=naik satu tingkat (2x lebih tajam), 2=maksimum 1024\n"
"ResolutionBoost=0\n"
"; Hanya kamera shadow berukuran asli di rentang ini yang dinaikkan (px). Ukuran asli di game: 16, 256, 512\n"
"BoostFromPx=256\n"
"BoostToPx=512\n"
"; Paksa bayangan real-time (kualitas tinggi) menyala di semua HP: 0=mati, 1=paksa\n"
"ForceAdvancedShadows=0\n"
"; Kegelapan bayangan: 1.0=mati, contoh 1.3\n"
"StrengthScale=1.0\n"
"; Jarak max bayangan ped: 0=mati, contoh 40.0\n"
"PedShadowDistance=0\n";

static char gIniPath[512];
static char gLogPath[512];

static void BuildPaths()
{
    const char* dir = aml->GetConfigPath();
    if (!dir || !*dir) dir = "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/configs/";
    const char* sep = (dir[strlen(dir) - 1] == '/') ? "" : "/";
    snprintf(gIniPath, sizeof(gIniPath), "%s%sShadowMod.ini", dir, sep);
    snprintf(gLogPath, sizeof(gLogPath), "%s%sShadowMod.log", dir, sep);
}

// Log ke file (dibuka-tutup tiap baris supaya tetap tersimpan walau game crash) + logcat
static void FLog(const char* fmt, ...)
{
    char buf[400];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logger->Info("%s", buf);
    FILE* f = fopen(gLogPath, "a");
    if (f) { fputs(buf, f); fputc('\n', f); fclose(f); }
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
    else if (!strcasecmp(key, "ForceAdvancedShadows")) cfg.forceAdvanced = atoi(val) != 0;
    else if (!strcasecmp(key, "BoostFromPx"))       cfg.boostFromPx   = std::min(1024, std::max(1, atoi(val)));
    else if (!strcasecmp(key, "BoostToPx"))         cfg.boostToPx     = std::min(1024, std::max(1, atoi(val)));
    else if (!strcasecmp(key, "ResolutionBoost"))   cfg.resolutionBoost = std::min(2, std::max(0, atoi(val)));
    else if (!strcasecmp(key, "StrengthScale"))     cfg.strengthScale = std::min(3.0f,   std::max(0.0f, (float)atof(val)));
    else if (!strcasecmp(key, "PedShadowDistance")) cfg.pedDistance   = std::min(200.0f, std::max(0.0f, (float)atof(val)));
}

static void LoadConfig()
{
    BuildPaths();
    FILE* lf = fopen(gLogPath, "w");   // kosongkan log lama
    if (lf) fclose(lf);

    FILE* f = fopen(gIniPath, "r");
    if (!f)
    {
        f = fopen(gIniPath, "w");
        if (f) { fputs(kDefaultIni, f); fclose(f); FLog("ShadowMod.ini default dibuat: %s", gIniPath); }
        else   { FLog("GAGAL membuat ShadowMod.ini di %s", gIniPath); }
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) ParseLine(line);
    fclose(f);
}

// ============================ HOOKS ============================
static void* hGTASA = nullptr;

static uint8_t* pShadowStrength      = nullptr; // CTimeCycle::m_nShadowStrength      [8*23]
static uint8_t* pLightShadowStrength = nullptr; // CTimeCycle::m_nLightShadowStrength [8*23]
static uint8_t* pPoleShadowStrength  = nullptr; // CTimeCycle::m_nPoleShadowStrength  [8*23]
static float*   pPedShadowDist       = nullptr; // MAX_DISTANCE_PED_SHADOWS
static float*   pPedShadowDistSqr    = nullptr; // MAX_DISTANCE_PED_SHADOWS_SQR
static uint8_t* pMobileSettings     = nullptr; // MobileSettings::settings (struct 1184 byte)
static const int kAdvShadowOfs = 0xA8;          // field yang dibandingkan dengan 2 oleh UseAdvancedShadows / DoShadowThisFrame
static bool     advLogged = false, advForcedLogged = false;

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
// Parameter 'size' adalah EKSPONEN pangkat dua (9 = 512 px, 8 = 256 px), bukan piksel.
static void* Create_hook(void* self, int size)
{
    static int calls = 0;
    int newSize = size;
    bool boosted = false;
    if (size >= 4 && size <= 10)
    {
        int px = 1 << size;
        if (px >= cfg.boostFromPx && px <= cfg.boostToPx)
        {
            newSize = std::min(10, size + cfg.resolutionBoost);   // maks 2^10 = 1024 px
            boosted = (newSize != size);
        }
    }
    if (calls < 24) FLog("CShadowCamera::Create #%d: eksp %d -> %d (%d px -> %d px) %s",
                         ++calls, size, newSize, 1 << (size & 15), 1 << (newSize & 15),
                         boosted ? "[DINAIKKAN]" : "[dibiarkan]");
    return Create_orig(self, newSize);
}

static void (*Process_orig)() = nullptr;
static void Process_hook()
{
    static unsigned frames = 0;
    ++frames;
    bool mark = (frames <= 3) || frames == 10 || frames == 30 || frames == 100 || frames == 300 ||
                frames == 1000 || frames == 3000 || frames == 10000;
    if (mark) FLog("frame #%u masuk", frames);

    if (cfg.strengthScale != 1.0f && pShadowStrength && pLightShadowStrength && pPoleShadowStrength)
    {
        if (!snapshotTaken)
        {
            memcpy(origShadow, pShadowStrength,      kTCSize);
            memcpy(origLight,  pLightShadowStrength, kTCSize);
            memcpy(origPole,   pPoleShadowStrength,  kTCSize);
            snapshotTaken = true;
            FLog("Snapshot timecycle shadow diambil");
        }
        ScaleArray(pShadowStrength,      origShadow, cfg.strengthScale);
        ScaleArray(pLightShadowStrength, origLight,  cfg.strengthScale);
        ScaleArray(pPoleShadowStrength,  origPole,   cfg.strengthScale);
    }
    if (pMobileSettings)
    {
        volatile int* adv = (volatile int*)(pMobileSettings + kAdvShadowOfs);
        if (!advLogged) { FLog("MobileSettings+0x%X nilai awal = %d  (2 = bayangan real-time aktif)", kAdvShadowOfs, (int)*adv); advLogged = true; }
        if (cfg.forceAdvanced && *adv != 2)
        {
            *adv = 2;
            if (!advForcedLogged) { FLog("MobileSettings+0x%X dipaksa jadi 2", kAdvShadowOfs); advForcedLogged = true; }
        }
    }
    if (cfg.pedDistance > 0.0f)
    {
        if (pPedShadowDist)    *pPedShadowDist    = cfg.pedDistance;
        if (pPedShadowDistSqr) *pPedShadowDistSqr = cfg.pedDistance * cfg.pedDistance;
    }

    Process_orig();
    if (mark) FLog("frame #%u selesai", frames);
}

static void* Sym(const char* name)
{
    void* p = dlsym(hGTASA, name);
    FLog("simbol %s = %p", name, p);
    return p;
}

extern "C" void OnModPreLoad()
{
    logger->SetTag("ShadowMod");
}

extern "C" void OnModLoad()
{
    LoadConfig();
    FLog("ShadowMod v1.5 start. enabled=%d resBoost=%d [%d..%d px] strength=%.2f pedDist=%.1f forceAdv=%d",
         cfg.enabled, cfg.resolutionBoost, cfg.boostFromPx, cfg.boostToPx, cfg.strengthScale, cfg.pedDistance, cfg.forceAdvanced);

    bool needRes  = cfg.resolutionBoost > 0;
    bool needProc = (cfg.strengthScale != 1.0f) || (cfg.pedDistance > 0.0f) || cfg.forceAdvanced;

    if (!cfg.enabled || (!needRes && !needProc))
    {
        FLog("Tidak ada fitur aktif -> tidak ada hook dipasang. Selesai.");
        return;
    }

    hGTASA = dlopen("libGTASA.so", RTLD_LAZY);
    FLog("dlopen libGTASA.so = %p", hGTASA);
    if (!hGTASA) return;

    if (needProc)
    {
        pShadowStrength      = (uint8_t*)Sym("_ZN10CTimeCycle17m_nShadowStrengthE");
        pLightShadowStrength = (uint8_t*)Sym("_ZN10CTimeCycle22m_nLightShadowStrengthE");
        pPoleShadowStrength  = (uint8_t*)Sym("_ZN10CTimeCycle21m_nPoleShadowStrengthE");
        pPedShadowDist       = (float*)  Sym("MAX_DISTANCE_PED_SHADOWS");
        pPedShadowDistSqr    = (float*)  Sym("MAX_DISTANCE_PED_SHADOWS_SQR");
        pMobileSettings      = (uint8_t*)Sym("_ZN14MobileSettings8settingsE");

        void* fProcess = Sym("_ZN5CGame7ProcessEv");
        if (fProcess)
        {
            FLog("memasang hook CGame::Process ...");
            aml->Hook(fProcess, (void*)Process_hook, (void**)&Process_orig);
            FLog("hook CGame::Process terpasang, orig=%p", (void*)Process_orig);
        }
    }

    if (needRes)
    {
        void* fCreate = Sym("_ZN13CShadowCamera6CreateEi");
        if (fCreate)
        {
            FLog("memasang hook CShadowCamera::Create ...");
            aml->Hook(fCreate, (void*)Create_hook, (void**)&Create_orig);
            FLog("hook CShadowCamera::Create terpasang, orig=%p", (void*)Create_orig);
        }
    }

    FLog("OnModLoad selesai");
}
