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
//
// For each one this plugin builds a MissionData (the game's mission asset) with a "Free Roam" mode (no sublevels,
// so no AI) and appends it to BP_GameInstance.Missions, the list the mission menu shows. The PvP plugin then adds
// its PvP mode to it like to any other mission. Travel is the game's own: TravelTo(LevelName, "Default", ...).
#include <GML/GML.hpp>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace gml;

#define CUSTOMMAPS_VERSION "0.1.0"
GML_PLUGIN("com.remarrow.custommaps", "Custom Maps", CUSTOMMAPS_VERSION);

struct Spawn { std::string name; double x = 0, y = 0; };
struct MapDef { std::string folder, name, level, like = "DA_WinterVillage"; double mapX = 900, mapY = 500; std::vector<Spawn> spawns; };

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

GML_AWAKE() {
    FindMaps();
    Hook("BP_GameInstance_C:GetItems", [](Object, GUFunction*, void*) { AddMaps(); return false; });  // before: the PvP plugin adds its mode after
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void*) { AddMaps(); });
    Log("Custom Maps {} - {} map(s) in {}", CUSTOMMAPS_VERSION, s_maps.size(), ModsDir().string());
    return 0;
}
