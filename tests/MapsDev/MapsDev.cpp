// MapsDev - automated in-game test of the Custom Maps plugin. Not shipped; installed only for a run.
// Team room: finds the custom mission ([Test] Level) in the game's mission list, starts it as the mission menu does
// (SelectMission with its first mode, then the game mode's TravelTo), then in the map logs what loaded: the level,
// the player starts, the static meshes and their materials, and where the player is over 10 s (on the floor, not
// falling through).
#include <GML/GML.hpp>
#include <windows.h>
#include <cmath>
#include <format>
#include <map>

using namespace gml;

GML_PLUGIN("com.remarrow.mapsdev", "MapsDev", "0.1.0", {{"com.remarrow.custommaps", "0.1.0", GML_DEPENDENCY_SOFT}});

struct Vec { double x, y, z; };
static std::string s_level;
static std::string s_lvlNow;
static ULONGLONG s_at = 0;
static int s_step = 0;
static bool s_preload = true;

static std::string Txt(const void* t) { char b[256] = ""; if (t) API->TextToString(t, b, sizeof b); return b; }
static std::string NameStr(uint64_t n) { char b[256] = ""; API->NameToString(n, b, sizeof b); return b; }
static Object Pawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}
static Vec Loc(Object a) { Params p(a, "K2_GetActorLocation"); p.Call(); return p.Return<Vec>(); }
static std::string LevelPath() {
    Params gs(Lib("GameplayStatics"), "GetGameState");
    gs.SetObj("WorldContextObject", Object(API->WorldContext())).Call();
    Object s = gs.Return<GUObject*>();
    return s ? s.Path() : "";
}
static std::vector<Object> ActorsOf(const char* cls) {
    std::vector<Object> out;
    Params p(Lib("GameplayStatics"), "GetAllActorsOfClass");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", FindClass(cls)).Call();
    if (auto* arr = (GML_TArray*)p.Ptr("OutActors"))
        for (int i = 0; i < arr->Num; i++) out.push_back(((GUObject**)arr->Data)[i]);
    return out;
}
static double T() { return (GetTickCount64() - s_at) / 1000.0; }

static void Room() {
    if (s_step == 0 && T() > 10) {
        s_step = 1;
        Object gi = FindFirstOf("BP_GameInstance_C");
        auto* list = (GML_TArray*)gi.PropPtr("Missions");
        Object mission;
        std::string names;
        for (int i = 0; i < list->Num; i++) {
            Object m(((GUObject**)list->Data)[i]);
            names += Txt(m.PropPtr("MissionName")) + " (" + NameStr(m.Get<uint64_t>("LevelName")) + "), ";
            if (NameStr(m.Get<uint64_t>("LevelName")) == s_level) mission = m;
        }
        Log("MAPS mission list ({}): {}", list->Num, names);
        if (!mission) { Log("MAPS no mission for {}", s_level); Log("DEV TEST COMPLETE"); return; }
        GFProperty* gmp = API->FindProperty(mission.Struct(), "GameModes");
        GML_PropInfo pi{}, ei{};
        API->GetPropertyInfo(gmp, &pi);
        API->GetPropertyInfo(pi.inner, &ei);
        auto* modes = (GML_TArray*)mission.PropPtr("GameModes");
        std::string mn;
        for (int i = 0; i < modes->Num; i++) mn += Txt((uint8_t*)modes->Data + (size_t)i * ei.size) + ", ";
        Log("MAPS '{}' modes: {}", Txt(mission.PropPtr("MissionName")), mn);
        GUFunction* fn = API->FindFunction(gi.Struct(), "SelectMission");
        std::vector<uint8_t> buf(API->StructSize((GUStruct*)fn) + 16, 0);
        for (GFProperty* p = API->NextProperty((GUStruct*)fn, nullptr); p; p = API->NextProperty((GUStruct*)fn, p)) {
            char n[64] = "";
            GML_PropInfo i{};
            API->GetPropertyName(p, n, sizeof n);
            API->GetPropertyInfo(p, &i);
            if (!strcmp(n, "MissionData")) *(GUObject**)(buf.data() + i.offset) = mission.ptr;
            else if (!strcmp(n, "MissionParams")) buf[i.offset] = 1;
            else if (!strcmp(n, "GameMode") && modes->Num > 0) std::memcpy(buf.data() + i.offset, modes->Data, ei.size);
        }
        API->ProcessEvent(gi.ptr, fn, buf.data());
        Params gm(Lib("GameplayStatics"), "GetGameMode");
        gm.SetObj("WorldContextObject", Object(API->WorldContext())).Call();
        Params tt(Object(gm.Return<GUObject*>()), "TravelTo");
        std::wstring lv(s_level.begin(), s_level.end());
        // is the map plugin mounted? its assets, loaded directly ([Test] Preload)
        if (s_preload)
        for (std::string a : {s_level + "." + s_level.substr(s_level.rfind('/') + 1), std::string("/MapTest/Meshes/SM_Block.SM_Block"), std::string("/MapTest/Materials/M_Block.M_Block")}) {
            Object o = LoadObject(a.c_str());
            Log("MAPS LoadObject {} -> {}", a, o ? o.FullName() : std::string("null"));
        }
        tt.SetString("LevelName", lv).SetString("GameMode", L"Default").Call();
        Log("MAPS SelectMission + TravelTo({}, Default)", s_level);
    }
    if (s_step == 1 && T() > 25) {  // still here: the engine's own OpenLevel
        s_step = 2;
        Params p(Lib("GameplayStatics"), "OpenLevel");
        p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("LevelName", API->MakeName(s_level.c_str())).Set("bAbsolute", true)
            .SetString("Options", L"game=Default").Call();
        Log("MAPS TravelTo did nothing in 15 s: OpenLevel({}?game=Default)", s_level);
    }
    if (s_step == 2 && T() > 45) { s_step = 3; Log("MAPS still in the team room after OpenLevel"); Log("DEV TEST COMPLETE"); }
}

static void InMap() {
    static int phase = 0;
    static std::vector<Vec> track;
    if (phase == 0 && T() > 4) {
        phase = 1;
        auto starts = ActorsOf("PlayerStart");
        std::string st;
        for (auto& s : starts) { Vec l = Loc(s); st += std::format("{} '{}' ({:.0f},{:.0f},{:.0f}) ", s.Name(), NameStr(s.Get<uint64_t>("PlayerStartTag")), l.x, l.y, l.z); }
        Log("MAPS in {}: {} player start(s): {}", s_lvlNow, starts.size(), st);
        auto sms = ActorsOf("StaticMeshActor");
        int ok = 0;
        std::map<std::string, int> mats;
        for (auto& a : sms) {
            Object c = a.GetObj("StaticMeshComponent");
            Object mesh = c ? c.GetObj("StaticMesh") : Object();
            if (mesh) ok++;
            Params gm(c, "GetMaterial");
            gm.Set("ElementIndex", (int32_t)0).Call();
            mats[Object(gm.Return<GUObject*>()).Name()]++;
        }
        std::string ms;
        for (auto& [k, v] : mats) ms += std::format("{} x{}, ", k, v);
        Log("MAPS {} static mesh actor(s), {} with a mesh; materials: {}", sms.size(), ok, ms);
        for (const char* cls : {"DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog", "BP_AICharacter_C"})
            Log("MAPS {}: {}", cls, ActorsOf(cls).size());
        // Collision: how the meshes' collision is set up, and what a downward trace at the first start hits.
        std::map<std::string, int> prof;
        int logged = 0;
        Vec s0 = starts.empty() ? Vec{} : Loc(starts[0]);
        for (auto& a : sms) {
            Object c = a.GetObj("StaticMeshComponent");
            if (!c) continue;
            Params ce(c, "GetCollisionEnabled");
            ce.Call();
            Params pn(c, "GetCollisionProfileName");
            pn.Call();
            prof[std::format("{}/{}", NameStr(pn.Return<uint64_t>()), ce.Return<uint8_t>())]++;
            Object mesh = c.GetObj("StaticMesh");
            if (!mesh || mesh.Name().find("Collider") == std::string::npos || logged >= 6) continue;
            Vec l = Loc(a);
            // the collider whose bounds hold the first start
            Params gb(a, "GetActorBounds");
            gb.Set("bOnlyCollidingComponents", false).Call();
            Vec bo = *(Vec*)gb.Ptr("Origin"), be = *(Vec*)gb.Ptr("BoxExtent");
            if (std::abs(bo.x - s0.x) > be.x || std::abs(bo.y - s0.y) > be.y) continue;
            Log("MAPS collider bounds origin ({:.0f},{:.0f},{:.0f}) extent ({:.0f},{:.0f},{:.0f}), hidden {}, scale/rot from actor at ({:.0f},{:.0f},{:.0f})",
                bo.x, bo.y, bo.z, be.x, be.y, be.z, c.GetBool("bHiddenInGame"), l.x, l.y, l.z);
            logged++;
            Object bs = mesh.GetObj("BodySetup");
            std::string bsInfo = bs ? std::format("BodySetup {} trace flag {} ", bs.Name(), bs.Get<uint8_t>("CollisionTraceFlag")) : "NO BodySetup ";
            Params tc(c, "K2_LineTraceComponent");
            tc.Set("TraceStart", Vec{s0.x, s0.y, s0.z + 5000}).Set("TraceEnd", Vec{s0.x, s0.y, s0.z - 50000}).Set("bTraceComplex", true).Call();
            Params ts(c, "K2_LineTraceComponent");
            ts.Set("TraceStart", Vec{s0.x, s0.y, s0.z + 5000}).Set("TraceEnd", Vec{s0.x, s0.y, s0.z - 50000}).Set("bTraceComplex", false).Call();
            Log("MAPS chunk {} mesh {} at ({:.0f},{:.0f},{:.0f}): {}collision {} profile {}, trace on it complex {} simple {}",
                a.Name(), mesh.Name(), l.x, l.y, l.z, bsInfo, ce.Return<uint8_t>(), NameStr(pn.Return<uint64_t>()), tc.Return<bool>(), ts.Return<bool>());
        }
        std::string ps;
        for (auto& [k, v] : prof) ps += std::format("{} x{}, ", k, v);
        Log("MAPS collision profile/enabled: {}", ps);
        Params lt(Lib("KismetSystemLibrary"), "LineTraceSingle");
        lt.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Start", Vec{s0.x, s0.y, s0.z + 5000}).Set("End", Vec{s0.x, s0.y, s0.z - 50000})
            .Set("TraceChannel", (uint8_t)0).Set("bTraceComplex", true).Set("bIgnoreSelf", true).Call();
        Params bh(Lib("GameplayStatics"), "BreakHitResult");
        GML_PropInfo hi{};
        API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("GameplayStatics").Struct(), "BreakHitResult"), "Hit"), &hi);
        std::memcpy(bh.Ptr("Hit"), lt.Ptr("OutHit"), hi.size);
        bh.Call();
        Vec hl = *(Vec*)bh.Ptr("Location");
        Object ha(*(GUObject**)bh.Ptr("HitActor")), hc(*(GUObject**)bh.Ptr("HitComponent"));
        Object hm = hc ? hc.GetObj("StaticMesh") : Object();
        Log("MAPS world trace down at the first start: {} at z {:.0f}, actor {} mesh {}", lt.Return<bool>(), hl.z, ha.Name(), hm.Name());
        // Sweeps like the character's: a capsule down from above and up from below, and a line up from below.
        // If the line down hits but the capsule down doesn't (or only the upward ones do), the colliders' triangles
        // face down (winding flipped by the axis conversion).
        auto sweep = [&](const char* what, double z0, double z1, bool capsule) {
            Params t(Lib("KismetSystemLibrary"), capsule ? "CapsuleTraceSingle" : "LineTraceSingle");
            t.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Start", Vec{s0.x, s0.y, z0}).Set("End", Vec{s0.x, s0.y, z1})
                .Set("TraceChannel", (uint8_t)0).Set("bTraceComplex", false).Set("bIgnoreSelf", true);
            if (capsule) t.Set("Radius", 34.0f).Set("HalfHeight", 88.0f);
            t.Call();
            Params b(Lib("GameplayStatics"), "BreakHitResult");
            std::memcpy(b.Ptr("Hit"), t.Ptr("OutHit"), hi.size);
            b.Call();
            Object c(*(GUObject**)b.Ptr("HitComponent"));
            Object m = c ? c.GetObj("StaticMesh") : Object();
            Vec n = *(Vec*)b.Ptr("ImpactNormal");
            Log("MAPS {}: {} at z {:.0f} on {} (normal z {:.2f}, initial overlap {})", what, t.Return<bool>(), (*(Vec*)b.Ptr("Location")).z,
                m.Name(), n.z, *(bool*)b.Ptr("bInitialOverlap"));
        };
        sweep("capsule down", s0.z + 500, s0.z - 3000, true);
        sweep("capsule up", s0.z - 3000, s0.z + 500, true);
        sweep("line down (simple)", s0.z + 500, s0.z - 3000, false);
        sweep("line up (simple)", s0.z - 3000, s0.z + 500, false);
        // The player's capsule and what it collides with.
        if (Object pawn = Pawn()) {
            Object cap = pawn.GetObj("CapsuleComponent");
            if (cap) {
                Params ce(cap, "GetCollisionEnabled"); ce.Call();
                Params pn(cap, "GetCollisionProfileName"); pn.Call();
                Params ot(cap, "GetCollisionObjectType"); ot.Call();
                Params rs(cap, "GetCollisionResponseToChannel"); rs.Set("Channel", (uint8_t)0).Call();  // WorldStatic
                Log("MAPS pawn capsule: collision {} profile {} object type {} response to WorldStatic {}", ce.Return<uint8_t>(),
                    NameStr(pn.Return<uint64_t>()), ot.Return<uint8_t>(), rs.Return<uint8_t>());
            }
            Object mv = pawn.GetObj("CharacterMovement");
            if (mv) Log("MAPS movement mode {} gravity {:.2f}", mv.Get<uint8_t>("MovementMode"), mv.Get<float>("GravityScale"));
        }
    }
    if (phase >= 1 && phase < 12 && T() > 4 + phase) {
        Object p = Pawn();
        Vec l = p ? Loc(p) : Vec{};
        Log("MAPS t+{} pawn {} at ({:.0f},{:.0f},{:.0f})", phase, p.Name(), l.x, l.y, l.z);
        phase++;
        if (phase == 12) Log("DEV TEST COMPLETE");
    }
}

GML_AWAKE() {
    s_preload = Config.Bind("Test", "Preload", true, "Load the map's assets before travelling.").Value();
    s_level = Config.Bind<std::string>("Test", "Level", "/MapTest/Maps/MapTest", "The custom map to travel to.").Value();
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void* gm) {
        std::string level = Object((GUObject*)gm).Path();
        if (level.find("/Game/Maps/MainMenu/") == 0) {
            Params p(Lib("GameplayStatics"), "OpenLevel");
            p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("LevelName", API->MakeName("/Game/Maps/TeamRoom/Teamroom")).Set("bAbsolute", true).Call();
        }
    });
    On(GML_EVENT_TICK, [](void*) {
        std::string lv = LevelPath();
        if (lv != s_lvlNow) { s_lvlNow = lv; s_at = GetTickCount64(); Log("MAPS level {}", lv); }
        if (lv.find("Teamroom") != std::string::npos) Room();
        else if (!s_level.empty() && lv.rfind(s_level, 0) == 0) InMap();
    });
    return 0;
}
