// CustomMaps - GML plugin (com.remarrow.custommaps). Puts custom maps in Geronimo's mission menu.
//
// A custom map is a cooked content plugin in <game>\Geronimo\Mods\<Name>\ (the engine mounts it by itself, like the
// RD704 gun), with a GeronimoMap.ini next to its .uplugin:
//
//   Name = Map Test                     ; the mission's name in the menu
//   Level = /MapTest/Maps/MapTest       ; the map's package path
//   MapX = 900                          ; where its pin sits on the mission menu's world map
//   MapY = 500
//   Spawn = North, 712, 108             ; a spawn the players can pick: name (= the PlayerStart's tag), x, y on the tactical map
//   Spawn = South, 552, 568
//   Like = DA_WinterVillage             ; a game mission to borrow the thumbnail and tactical map picture from
//   Ambience = /Game/Audio/Ambient/Villages/Winter_Map, 1.0   ; optional: a sound looped in the map (volume)
//   Reverb = /Game/Developers/Meat/Resonance/Maps/Winter/R_WV_Outdoor   ; optional: Resonance reverb preset (gunshot echo)
//   ReverbZone = /Game/Developers/Meat/Resonance/Maps/Winter/R_WV_IndoorConcrete, 8660, 6478, -704, 9781, 7533, -284
//                                       ; optional, any number: another preset inside a box (min x y z, max x y z, cm)
//
// For each one this plugin builds a MissionData (the game's mission asset) with a "Free Roam" mode (no sublevels,
// so no AI) and appends it to BP_GameInstance.Missions, the list the mission menu shows. The PvP plugin then adds
// its PvP mode to it like to any other mission. Travel is the game's own: TravelTo(LevelName, "Default", ...).
//
// The game's maps place their ambience as an AmbientSound in the level (Winter Village: Winter_Map, a wind loop).
// A map cooked outside the game can't easily reference /Game sounds, so Ambience names one and this plugin plays it
// as a 2D sound when that map's world begins, restarting it if it ever stops.
#include <GML/GML.hpp>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace gml;

#define CUSTOMMAPS_VERSION "0.3.0"
GML_PLUGIN("com.remarrow.custommaps", "Custom Maps", CUSTOMMAPS_VERSION);

struct Spawn { std::string name; double x = 0, y = 0; };
struct ReverbZone { std::string preset; double mn[3] = {}, mx[3] = {}; };
struct MapDef {
    std::string folder, name, level, like = "DA_WinterVillage", ambience, reverb;
    double mapX = 900, mapY = 500;
    float ambienceVolume = 1.0f;
    std::vector<Spawn> spawns;
    std::vector<ReverbZone> zones;
};

// a bare package path gets its object name: /Game/A/B -> /Game/A/B.B
static std::string ObjectPath(std::string p) {
    if (!p.empty() && p.find('.') == std::string::npos) p += "." + p.substr(p.rfind('/') + 1);
    return p;
}

static std::vector<MapDef> s_maps;
static std::vector<GUObject*> s_missions;  // ours, kept alive

static std::string Trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// <game>\Geronimo\Mods: the exe is <game>\Geronimo\Binaries\Win64\Geronimo-Win64-Shipping.exe
static std::filesystem::path ModsDir() {
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path().parent_path().parent_path() / L"Mods";
}

static void FindMaps() {
    s_maps.clear();
    std::error_code ec;
    for (auto& dir : std::filesystem::directory_iterator(ModsDir(), ec)) {
        auto ini = dir.path() / L"GeronimoMap.ini";
        if (!std::filesystem::exists(ini, ec)) continue;
        if (std::filesystem::exists(dir.path() / L"disabled.txt", ec)) continue;
        MapDef m;
        m.folder = dir.path().filename().string();
        std::ifstream f(ini);
        for (std::string line; std::getline(f, line);) {
            line = Trim(line.substr(0, line.find(';')));
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
            if (k == "Name") m.name = v;
            else if (k == "Level") m.level = v;
            else if (k == "Like") m.like = v;
            else if (k == "MapX") m.mapX = atof(v.c_str());
            else if (k == "MapY") m.mapY = atof(v.c_str());
            else if (k == "Ambience") {
                size_t c = v.find(',');
                m.ambience = Trim(v.substr(0, c));
                if (c != std::string::npos) m.ambienceVolume = (float)atof(v.c_str() + c + 1);
                m.ambience = ObjectPath(m.ambience);
            }
            else if (k == "Reverb") m.reverb = ObjectPath(v);
            else if (k == "ReverbZone") {  // preset, minX, minY, minZ, maxX, maxY, maxZ (world cm)
                ReverbZone z;
                std::stringstream ss(v);
                std::string part;
                std::getline(ss, part, ',');
                z.preset = ObjectPath(Trim(part));
                int n = 0;
                for (; n < 6 && std::getline(ss, part, ','); n++) (n < 3 ? z.mn[n] : z.mx[n - 3]) = atof(part.c_str());
                if (n == 6 && !z.preset.empty()) m.zones.push_back(z);
                else Warn("{}: ReverbZone needs a preset and six numbers: {}", m.folder, v);
            }
            else if (k == "Spawn") {
                Spawn s;
                std::stringstream ss(v);
                std::string part;
                std::getline(ss, part, ',');
                s.name = Trim(part);
                if (std::getline(ss, part, ',')) s.x = atof(part.c_str());
                if (std::getline(ss, part, ',')) s.y = atof(part.c_str());
                if (!s.name.empty()) m.spawns.push_back(s);
            }
        }
        if (m.name.empty() || m.level.empty()) { Warn("{}: GeronimoMap.ini needs Name and Level", m.folder); continue; }
        s_maps.push_back(m);
        Log("found map '{}' ({}) in Mods\\{} with {} spawn(s)", m.name, m.level, m.folder, m.spawns.size());
    }
}

// FGamemodeStruct / FSpawnStruct fields carry GUID suffixes: found by prefix.
static int FieldOffset(GUStruct* st, const char* prefix, GML_PropInfo* info = nullptr) {
    for (GFProperty* p = API->NextProperty(st, nullptr); p; p = API->NextProperty(st, p)) {
        char n[128] = "";
        GML_PropInfo i{};
        API->GetPropertyName(p, n, sizeof n);
        API->GetPropertyInfo(p, &i);
        if (std::string(n).rfind(prefix, 0) == 0) {
            if (info) *info = i;
            return i.offset;
        }
    }
    return -1;
}

static void MakeFString(GML_FString* out, const std::string& s) {
    std::memset(out, 0, sizeof *out);
    std::wstring w(s.begin(), s.end());
    if (auto* d = (wchar_t*)API->ArrayAdd((GML_TArray*)out, sizeof(wchar_t), (int)w.size() + 1)) std::memcpy(d, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
}

static std::string TextOf(const void* t) { char b[256] = ""; if (t) API->TextToString(t, b, sizeof b); return b; }
static std::string NameStr(uint64_t n) { char b[256] = ""; API->NameToString(n, b, sizeof b); return b; }

static GUObject* BuildMission(Object gi, const MapDef& m) {
    GUClass* cls = FindClass("MissionData_C");
    if (!cls) { Error("MissionData_C not found"); return nullptr; }
    Object like;
    auto* list = (GML_TArray*)gi.PropPtr("Missions");
    for (int i = 0; list && i < list->Num; i++)
        if (Object o(((GUObject**)list->Data)[i]); o && o.Name() == m.like) like = o;
    Object mis = NewObject(cls, gi);
    if (!mis) return nullptr;
    API->KeepAlive(mis.ptr);
    struct V2 { double x, y; };
    mis.Set("MapPosition", V2{m.mapX, m.mapY});
    *(uint64_t*)mis.PropPtr("LevelName") = API->MakeName(m.level.c_str());
    API->MakeText(m.name.c_str(), mis.PropPtr("MissionName"));
    if (like) mis.Set<GUObject*>("Thumbnail", like.Get<GUObject*>("Thumbnail"));
    // one mode: Free Roam, the game's "Default" mode string, no sublevels (no AI), our spawns
    GFProperty* gmp = API->FindProperty(mis.Struct(), "GameModes");
    GML_PropInfo pi{}, ei{};
    if (!gmp || !API->GetPropertyInfo(gmp, &pi) || !pi.inner || !API->GetPropertyInfo(pi.inner, &ei) || !ei.structType) return mis.ptr;
    GML_PropInfo spawnsInfo{}, spawnInner{};
    int oName = FieldOffset(ei.structType, "Name_"), oSub = FieldOffset(ei.structType, "Subtitle_"), oMode = FieldOffset(ei.structType, "Gamemode_"),
        oSpawns = FieldOffset(ei.structType, "AvailableSpawns_", &spawnsInfo), oMap = FieldOffset(ei.structType, "TacticalMap_");
    if (oName < 0 || oMode < 0 || oSpawns < 0 || !spawnsInfo.inner || !API->GetPropertyInfo(spawnsInfo.inner, &spawnInner) || !spawnInner.structType) {
        Warn("{}: the game mode struct is not the expected one - the mission has no modes", m.name);
        return mis.ptr;
    }
    auto* e = (uint8_t*)API->ArrayAdd((GML_TArray*)mis.PropPtr("GameModes"), ei.size, 1);
    std::memset(e, 0, ei.size);
    API->MakeText("Free Roam", e + oName);
    if (oSub >= 0) API->MakeText("Custom map, no AI", e + oSub);
    MakeFString((GML_FString*)(e + oMode), "Default");
    int sName = FieldOffset(spawnInner.structType, "Name_"), sPos = FieldOffset(spawnInner.structType, "MapPosition_"),
        sCap = FieldOffset(spawnInner.structType, "Capacity_");
    for (auto& s : m.spawns) {
        auto* se = (uint8_t*)API->ArrayAdd((GML_TArray*)(e + oSpawns), spawnInner.size, 1);
        std::memset(se, 0, spawnInner.size);
        if (sName >= 0) *(uint64_t*)(se + sName) = API->MakeName(s.name.c_str());
        if (sPos >= 0) *(V2*)(se + sPos) = {s.x, s.y};
        if (sCap >= 0) *(int32_t*)(se + sCap) = 4;
    }
    if (oMap >= 0 && like) {  // the borrowed mission's first mode's tactical map picture
        auto* lm = (GML_TArray*)like.PropPtr("GameModes");
        if (lm && lm->Num > 0) *(GUObject**)(e + oMap) = *(GUObject**)((uint8_t*)lm->Data + oMap);
    }
    return mis.ptr;
}

static void AddMaps() {
    Object gi = FindFirstOf("BP_GameInstance_C");
    auto* list = gi ? (GML_TArray*)gi.PropPtr("Missions") : nullptr;
    if (!list) return;
    TArrayView<GUObject*> missions{list};
    for (auto& m : s_maps) {
        bool there = false;
        for (int i = 0; i < list->Num && !there; i++) {
            Object o(((GUObject**)list->Data)[i]);
            there = o && NameStr(o.Get<uint64_t>("LevelName")) == m.level;
        }
        if (there) continue;
        GUObject* mis = BuildMission(gi, m);
        if (mis && missions.Add(mis)) {
            s_missions.push_back(mis);
            Log("*** '{}' added to the mission list ({} missions) ***", m.name, list->Num);
        }
    }
}

// ---- ambience
static GUObject* s_ambience = nullptr;   // the playing AudioComponent (the world owns it)
static GUObject* s_ambienceWorld = nullptr;
static const MapDef* s_ambienceMap = nullptr;
static float s_ambienceCheck = 0;

static bool s_ambienceConfirmed = false;

static void PlayAmbience(const MapDef& m) {
    Object snd = LoadObject(m.ambience.c_str());
    if (!snd) { Warn("{}: ambience {} not found", m.name, m.ambience); return; }
    Params p(Lib("GameplayStatics"), "SpawnSound2D");
    p.SetObj("WorldContextObject", Object(s_ambienceWorld)).SetObj("Sound", snd).Set("VolumeMultiplier", m.ambienceVolume)
        .Set("PitchMultiplier", 1.0f).Set("StartTime", 0.0f).Set("bPersistAcrossLevelTransition", false).Set("bAutoDestroy", false);
    p.Call();
    s_ambience = p.Return<GUObject*>();
    Log("{}: ambience {} {}", m.name, m.ambience, s_ambience ? "playing" : "failed to start");
}

// gm: the level's game mode (WORLD_BEGIN_PLAY's data), /Myrne/Maps/Myrne.Myrne:PersistentLevel.BP_GameModeBase_C_...
// (API->WorldContext() is the game instance: its path names no level.)
static void StartAmbience(GUObject* gm) {
    s_ambience = nullptr;
    s_ambienceMap = nullptr;
    s_ambienceConfirmed = false;
    s_ambienceWorld = gm;
    std::string world = Object(gm).Path();
    for (auto& m : s_maps)
        if (!m.ambience.empty() && world.rfind(m.level + ".", 0) == 0) {
            s_ambienceMap = &m;
            PlayAmbience(m);
        }
}

static void KeepAmbience(float dt) {
    if (!s_ambienceMap || (s_ambienceCheck += dt) < 2.0f) return;
    s_ambienceCheck = 0;
    if (!s_ambienceWorld || !API->IsValid(s_ambienceWorld)) { s_ambienceMap = nullptr; s_ambience = nullptr; return; }  // level left
    if (s_ambience && API->IsValid(s_ambience)) {
        Params ip(Object(s_ambience), "IsPlaying");
        if (ip.Call() && ip.Return<bool>()) {
            if (!s_ambienceConfirmed) { s_ambienceConfirmed = true; Log("{}: ambience still playing", s_ambienceMap->name); }
            return;
        }
        Log("{}: ambience stopped - restarting", s_ambienceMap->name);
        Params pl(Object(s_ambience), "Play");
        pl.Set("StartTime", 0.0f).Call();
    } else PlayAmbience(*s_ambienceMap);
}

// ---- reverb
// The game's maps give gunshots their echo with Resonance Audio reverb volumes (Winter Village: GVRAudioVolumes with
// presets like R_WV_Outdoor). A map cooked outside the game can't reference those presets, so Reverb names the one
// to use as Resonance's global preset while the map is loaded, and each ReverbZone swaps in another preset while the
// camera is inside its box (a bunker). The preset that was there before comes back when the map is left.
static const MapDef* s_reverbMap = nullptr;
static GUObject* s_reverbWorld = nullptr;
static GUObject* s_reverbBefore = nullptr;
static std::string s_reverbNow;
static float s_reverbCheck = 0;

static void SetGlobalReverb(GUObject* preset) {
    Params p(Lib("ResonanceAudioBlueprintFunctionLibrary"), "SetGlobalReverbPreset");
    p.SetObj("InPreset", Object(preset)).Call();
}

static bool UseReverb(const std::string& path) {
    if (path == s_reverbNow) return true;
    Object preset = LoadObject(path.c_str());
    if (!preset) { Warn("{}: reverb preset {} not found", s_reverbMap->name, path); s_reverbNow = path; return false; }
    API->KeepAlive(preset.ptr);
    SetGlobalReverb(preset.ptr);
    Log("{}: reverb {}", s_reverbMap->name, path.substr(path.rfind('/') + 1));
    s_reverbNow = path;
    return true;
}

static void EndReverb() {
    if (!s_reverbMap) return;
    SetGlobalReverb(s_reverbBefore);
    Log("{}: reverb back to {}", s_reverbMap->name, s_reverbBefore ? Object(s_reverbBefore).Name() : std::string("none"));
    s_reverbMap = nullptr;
    s_reverbNow.clear();
}

static void StartReverb(GUObject* gm) {
    EndReverb();
    std::string world = Object(gm).Path();
    for (auto& m : s_maps)
        if (!m.reverb.empty() && world.rfind(m.level + ".", 0) == 0) {
            Params g(Lib("ResonanceAudioBlueprintFunctionLibrary"), "GetGlobalReverbPreset");
            s_reverbBefore = g.Call() ? g.Return<GUObject*>() : nullptr;
            s_reverbMap = &m;
            s_reverbWorld = gm;
            UseReverb(m.reverb);
        }
}

static void KeepReverb(float dt) {
    if (!s_reverbMap || (s_reverbCheck += dt) < 0.25f) return;
    s_reverbCheck = 0;
    if (!s_reverbWorld || !API->IsValid(s_reverbWorld)) { EndReverb(); return; }  // level left
    if (s_reverbMap->zones.empty()) return;
    Params pc(Lib("GameplayStatics"), "GetPlayerCameraManager");
    pc.SetObj("WorldContextObject", Object(s_reverbWorld)).Set("PlayerIndex", 0);
    if (!pc.Call()) return;
    GUObject* cam = pc.Return<GUObject*>();
    if (!cam) return;
    Params cl(Object(cam), "GetCameraLocation");
    if (!cl.Call()) return;
    struct V3 { double x, y, z; } v = cl.Return<V3>();
    const double p[3] = {v.x, v.y, v.z};
    const std::string* want = &s_reverbMap->reverb;
    for (auto& z : s_reverbMap->zones)
        if (p[0] >= z.mn[0] && p[0] <= z.mx[0] && p[1] >= z.mn[1] && p[1] <= z.mx[1] && p[2] >= z.mn[2] && p[2] <= z.mx[2]) { want = &z.preset; break; }
    UseReverb(*want);
}

GML_AWAKE() {
    FindMaps();
    Hook("BP_GameInstance_C:GetItems", [](Object, GUFunction*, void*) { AddMaps(); return false; });  // before: the PvP plugin adds its mode after
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void* gm) { AddMaps(); StartAmbience((GUObject*)gm); StartReverb((GUObject*)gm); });
    On(GML_EVENT_TICK, [](void* d) { float dt = d ? *(float*)d : 0.0f; KeepAmbience(dt); KeepReverb(dt); });
    Log("Custom Maps {} - {} map(s) in {}", CUSTOMMAPS_VERSION, s_maps.size(), ModsDir().string());
    return 0;
}
