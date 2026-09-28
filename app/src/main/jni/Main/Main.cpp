//
// C-RAM Air Target Lead Indicator & Universal Unit Spawner (v4.5 - Ground & DF-21D Fixed)
//

#include <pthread.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <cctype>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <ctime>
#include <link.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../Include/KittyMemory/MemoryPatch.h"
#include "../Include/ImGui.h"
#include "../Include/Drawing.h"
#include "../Include/Unity.h"
#include "../Include/Math/Vector2.hpp"
#include "../Include/Math/Vector3.hpp"
#include "../Include/Math/Quaternion.hpp"
#include "../Include/Logger.h"

// -----------------------------------------------------------------------------
// RVAs from script.json & dump.cs (Relative to libil2cpp.so base)
// -----------------------------------------------------------------------------
#define RVA_CAMERA_MAIN          0x82D7D50
#define RVA_CAMERA_W2S           0x82D72A4
#define RVA_COMP_TRANSFORM       0x834A0F4
#define RVA_TRANS_POS            0x83601AC // UnityEngine.Transform.get_position
#define RVA_TRANS_SETPOS         0x8360280 // UnityEngine.Transform.set_position
#define RVA_TRANS_GETFORWARD     0x8360C08 // UnityEngine.Transform.get_forward
#define RVA_UNIT_TYPE            0x3F54748
#define RVA_PHYS_VEL             0x3EACAA4
#define RVA_HUD_LATEUPDATE       0x3F16414 // GeneralHUD.LateUpdate

// Multiplayer & UI Hooks
#define RVA_EVENTSYSTEM_UPDATE   0x86BE318 // UnityEngine.EventSystems.EventSystem.Update
#define RVA_MP_BOOTSTRAP         0x3E30D94 // MultiplayerService.Bootstrap
#define RVA_MP_ENTRY_CLICKED     0x3E2A764 // MultiplayerEntryButton.OnClicked

// Button / Shop Hooks
#define RVA_IAP_SHOP_ONENABLE    0x3DBD734 // IAPShopButton.OnEnable (Diamond icon in hangar)
#define RVA_ICONS_SHOWSTORE      0x4267774 // CBS.UI.IconsPanel.ShowStore (Backup)

// Unit Spawning & Catalog
#define RVA_ASSET_CATALOG_GET_PREFAB    0x3DA6210 // GameAssetCatalog.GetUnitPrefab(string idOrName)
#define RVA_SPAWN_MANAGER_AWAKE         0x3EC39F4 // SpawnManager.Awake
#define RVA_SPAWN_MANAGER_SPAWN_UNIT    0x3EC7308 // SpawnManager.SpawnUnit
#define RVA_SPAWN_MANAGER_SPAWN_AIR     0x3EC78DC // SpawnManager.SpawnAirUnit
#define RVA_SPAWN_MANAGER_SPAWN_GROUND  0x3EC7CBC // SpawnManager.SpawnGroundUnit
#define RVA_SPAWN_MANAGER_SNAP_GROUND   0x3EC7E78 // SpawnManager.SnapGroundUnitToSurface
#define RVA_OBJECT_INSTANTIATE          0x8355558 // UnityEngine.Object.Instantiate(Object original)

// Ground Component Fixes (TrackDeformer & RotatingSensor NullReferenceException guards)
#define RVA_TRACK_DEFORMER_UPDATE       0x3F68C20 // TrackDeformer.Update
#define RVA_ROTATING_SENSOR_UPDATE      0x3FB20FC // RotatingSensor.Update

// -----------------------------------------------------------------------------
// Field Offsets from dump.cs & il2cpp.h
// -----------------------------------------------------------------------------
#define OFFSET_HUD_PARENTSEAT    0x20
#define OFFSET_HUD_MAINCAMERA    0xE8
#define OFFSET_HUD_ALLENEMIES    0x140

#define OFFSET_SEAT_AWS          0x28

#define OFFSET_AWS_ALWAYSREADY   0x28
#define OFFSET_AWS_SELECTABLE    0x80

#define OFFSET_TURRET_MUZZLE     0xD8
#define OFFSET_TURRET_AMMOCONFIG 0xC0
#define OFFSET_TURRET_NOGRAVITY  0xFC

#define OFFSET_AMMO_SPEED        0x24

#define OFFSET_UNIT_ISALIVE      0xC4

// -----------------------------------------------------------------------------
// Function Pointer Types
// -----------------------------------------------------------------------------
typedef Vector3 (*t_Camera_WorldToScreenPoint)(void* camera, Vector3 position);
typedef void* (*t_Camera_get_main)();
typedef void* (*t_Component_get_transform)(void* component);
typedef Vector3 (*t_Transform_get_position)(void* transform);
typedef void (*t_Transform_set_position)(void* transform, Vector3 position, void* method);
typedef Vector3 (*t_Transform_get_forward)(void* transform, void* method);
typedef int (*t_IUnit_GetUnitType)(void* unit);
typedef Vector3 (*t_PhysicsObject_get_Velocity)(void* physicsObject);
typedef void (*t_GeneralHUD_LateUpdate)(void* self);

typedef void (*t_EventSystem_Update)(void* self);
typedef void (*t_MpBootstrap)();
typedef void (*t_MpEntry_OnClicked)(void* self);
typedef void (*t_IAPShopButton_OnEnable)(void* self);
typedef void (*t_IconsPanel_ShowStore)(void* self);

// Spawning function pointers
typedef void* (*t_il2cpp_string_new)(const char* str);
typedef void* (*t_GameAssetCatalog_GetUnitPrefab)(void* idOrName, void* method);
typedef void (*t_SpawnManager_Awake)(void* self);
typedef void* (*t_SpawnManager_SpawnUnit)(void* self, void* prefab, Vector3 pos, Quaternion rot, void* parent, bool setupAI, void* method);
typedef void* (*t_SpawnManager_SpawnAirUnit)(void* self, void* prefab, Vector3 pos, Quaternion rot, void* parent, float wayRoadHeight, float wayRoadScale, void* method);
typedef void* (*t_SpawnManager_SpawnGroundUnit)(void* self, void* prefab, Vector3 pos, Quaternion rot, void* parent, void* method);
typedef void (*t_SpawnManager_SnapGroundUnitToSurface)(void* self, void* unit, void* method);
typedef void* (*t_Object_Instantiate)(void* original, void* method);

// Component guard function pointers
typedef void (*t_TrackDeformer_Update)(void* self);
typedef void (*t_RotatingSensor_Update)(void* self);

static t_Camera_WorldToScreenPoint Camera_WorldToScreenPoint = nullptr;
static t_Camera_get_main Camera_get_main = nullptr;
static t_Component_get_transform Component_get_transform = nullptr;
static t_Transform_get_position Transform_get_position = nullptr;
static t_Transform_set_position Transform_set_position = nullptr;
static t_Transform_get_forward Transform_get_forward = nullptr;
static t_IUnit_GetUnitType IUnit_GetUnitType = nullptr;
static t_PhysicsObject_get_Velocity PhysicsObject_get_Velocity = nullptr;
static t_GeneralHUD_LateUpdate orig_GeneralHUD_LateUpdate = nullptr;

static t_EventSystem_Update orig_EventSystem_Update = nullptr;
static t_MpBootstrap MpBootstrap = nullptr;
static t_MpEntry_OnClicked MpEntry_OnClicked = nullptr;
static t_IAPShopButton_OnEnable orig_IAPShopButton_OnEnable = nullptr;
static t_IconsPanel_ShowStore orig_IconsPanel_ShowStore = nullptr;

static t_il2cpp_string_new il2cpp_string_new_fn = nullptr;
static t_GameAssetCatalog_GetUnitPrefab GameAssetCatalog_GetUnitPrefab = nullptr;
static t_SpawnManager_Awake orig_SpawnManager_Awake = nullptr;
static t_SpawnManager_SpawnUnit SpawnManager_SpawnUnit = nullptr;
static t_SpawnManager_SpawnAirUnit SpawnManager_SpawnAirUnit = nullptr;
static t_SpawnManager_SpawnGroundUnit SpawnManager_SpawnGroundUnit = nullptr;
static t_SpawnManager_SnapGroundUnitToSurface SpawnManager_SnapGroundUnitToSurface = nullptr;
static t_Object_Instantiate Object_Instantiate = nullptr;

static t_TrackDeformer_Update orig_TrackDeformer_Update = nullptr;
static t_RotatingSensor_Update orig_RotatingSensor_Update = nullptr;

// -----------------------------------------------------------------------------
// Global State & Buffers
// -----------------------------------------------------------------------------
static uintptr_t g_Il2CppBase = 0;
static void* g_SpawnManagerInstance = nullptr;

static std::atomic<bool> g_OpenMultiplayerRequested(false);
static uint64_t g_LastTriggerTime = 0;

struct SpawnRequest {
    std::string rawName;
    bool hasCustomPos;
    float customX, customY, customZ;
    float dist; // forward distance (default: -1 = auto)
    float alt;  // altitude (default: -1 = auto)
    bool setupAI; // default: true
};

static std::mutex g_SpawnMutex;
static std::vector<SpawnRequest> g_PendingSpawnQueue;
static std::atomic<bool> g_SpawnRequested(false);

struct ScreenLeadTarget {
    float leadX, leadY;
    float targetX, targetY;
    float distance;
    float flightTime;
    bool hasTargetLine;
};

#define MAX_TARGETS 128
static ScreenLeadTarget g_TargetsBuffer[2][MAX_TARGETS];
static int g_TargetsCount[2] = {0, 0};
static std::atomic<int> g_ActiveBufferIdx(0);

// Check if pointer looks like valid userspace memory on 64-bit Android
inline bool IsValidPtr(const void* ptr) {
    if (!ptr) return false;
    uintptr_t p = (uintptr_t)ptr & 0x00FFFFFFFFFFFFFFULL;
    return p >= 0x100000ULL && p <= 0x00007FFFFFFFFFFFULL;
}

static inline uint64_t getNowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static inline bool StartsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

inline Vector3 RotateVector(const Quaternion& q, const Vector3& v) {
    Vector3 u(q.X, q.Y, q.Z);
    float s = q.W;
    Vector3 uxv = Vector3::Cross(u, v);
    Vector3 u_uxv = Vector3::Cross(u, uxv);
    return v + (uxv * (2.0f * s)) + (u_uxv * 2.0f);
}

// -----------------------------------------------------------------------------
// Component Safety Guards (Eliminates NullReferenceExceptions on Ground Units)
// -----------------------------------------------------------------------------
void hook_TrackDeformer_Update(void* self) {
    if (!IsValidPtr(self)) return;
    void* initialVerts = *(void**)((uintptr_t)self + 0x40);
    void* currentVerts = *(void**)((uintptr_t)self + 0x58);
    // If vertices are not properly allocated by prefab, skip updating to prevent NullReferenceException
    if (!IsValidPtr(initialVerts) || !IsValidPtr(currentVerts)) {
        return;
    }
    if (orig_TrackDeformer_Update) {
        orig_TrackDeformer_Update(self);
    }
}

void hook_RotatingSensor_Update(void* self) {
    if (!IsValidPtr(self)) return;
    void* rotatingObj = *(void**)((uintptr_t)self + 0x48);
    void* sensorMio = *(void**)((uintptr_t)self + 0x20);
    // If target rotating object or sensor link is missing, skip updating
    if (!IsValidPtr(rotatingObj) || !IsValidPtr(sensorMio)) {
        return;
    }
    if (orig_RotatingSensor_Update) {
        orig_RotatingSensor_Update(self);
    }
}

// -----------------------------------------------------------------------------
// Unit Name Alias Resolver
// -----------------------------------------------------------------------------
std::string ResolveUnitAlias(const std::string& input) {
    std::string s = input;
    for (char &c : s) c = tolower((unsigned char)c);

    // Ballistic & Missile Launchers
    if (s == "df-21d" || s == "df21d" || s == "df21" || s == "df-21" || s == "df 21" || s == "df 21d") return "DF-21D";
    if (s == "shahedlauncher" || s == "launcher") return "ShahedLauncher";

    // Bombers & Drones
    if (s == "b2" || s == "b-2" || s == "spirit" || s == "b2 spirit" || s == "b-2 spirit") return "B-2 Spirit new";
    if (s == "shahed" || s == "shahed136" || s == "shahed-136") return "Shahed-136";
    if (s == "fpv" || s == "drone") return "FPV drone enemy";
    if (s == "ac130" || s == "ac-130" || s == "gunship") return "AC-130";

    // Air Defense Systems (Ground)
    if (s == "tor" || s == "tor-m1" || s == "tor-m2" || s == "9k332") return "9K332-Tor_new";
    if (s == "tunguska" || s == "2k22") return "2K22Tunguska_new";
    if (s == "pantsir" || s == "pantsir-s1") return "Pantsir";
    if (s == "buk" || s == "buk-m1") return "Buk-M1";
    if (s == "patriot" || s == "pac3" || s == "pac-3") return "Patriot";
    if (s == "shilka") return "Shilka_new";
    if (s == "vads" || s == "m163") return "M163 VADS";
    if (s == "pgz04" || s == "pgz-04") return "PGZ-04";
    if (s == "type625") return "Type625_new";
    if (s == "type87") return "Type87_new";

    // Fighters & Attack Jets
    if (s == "a10" || s == "a-10" || s == "warthog") return "new A-10 1";
    if (s == "su57" || s == "su-57" || s == "felon") return "Su-57 Felon";
    if (s == "su25" || s == "su-25" || s == "frogfoot") return "Su-25 1";
    if (s == "su27" || s == "su-27") return "Su-27";
    if (s == "su30" || s == "su-30") return "Su-30MKK";
    if (s == "mig29" || s == "mig-29") return "Mig-29 1";
    if (s == "f14" || s == "f-14" || s == "tomcat") return "F-14 Tomcat";
    if (s == "f15" || s == "f-15") return "F-15 1";
    if (s == "f16" || s == "f-16") return "F-16";
    if (s == "f18" || s == "f-18" || s == "hornet" || s == "f18f") return "F-18F Super Hornet";
    if (s == "f35" || s == "f-35" || s == "f35a") return "F-35A";
    if (s == "f4" || s == "f-4" || s == "phantom") return "F-4 Phantom 2";
    if (s == "j8" || s == "j-8") return "J-8";
    if (s == "j10" || s == "j-10" || s == "j10ce") return "J-10CE";
    if (s == "j20" || s == "j-20") return "J-20 Chengdu 2";
    if (s == "mirage" || s == "mirage2000") return "Mirage-2000 2";

    // Helicopters
    if (s == "ka52" || s == "ka-52" || s == "alligator") return "Ka-52";
    if (s == "mi28" || s == "mi-28" || s == "havoc") return "Mi-28";
    if (s == "mi24" || s == "mi-24" || s == "hind") return "Mi-24";
    if (s == "apache" || s == "ah64" || s == "ah-64") return "AH-64 Apache";
    if (s == "cobra" || s == "ah1" || s == "ah-1") return "AH-1 Cobra";

    // Armor & Ground Vehicles
    if (s == "abrams" || s == "m1" || s == "m1a2") return "M1Abrams_new 1";
    if (s == "t90" || s == "t-90" || s == "t90a") return "T-90A";
    if (s == "t72" || s == "t-72") return "T-72";
    if (s == "t72heli") return "T-72 Heli";
    if (s == "tonk") return "TONK";
    if (s == "ztq15" || s == "ztq-15") return "ZTQ-15";
    if (s == "type99") return "Type 99";
    if (s == "apc") return "APC_new";
    if (s == "kamaz") return "KAMAZ_new";
    if (s == "humvee") return "Humvee_new 2";
    if (s == "toyota" || s == "pickup") return "ToyotaPickup_new";

    // Naval & CIWS
    if (s == "destroyer") return "Destroyer";
    if (s == "frigate") return "Frigate";
    if (s == "kashtan") return "Kashtan";
    if (s == "searam" || s == "sea-ram") return "SEA-RAM";
    if (s == "phalanx") return "PhalanxHD";
    if (s == "goalkeeper") return "Goalkeeper";
    if (s == "ak630") return "AK630M-2-Duet";
    if (s == "flak38") return "Flak38_20mm";
    if (s == "zu23") return "Zu-23-2-landed";
    if (s == "soldier" || s == "infantry") return "Soldier2";

    return input; // return as-is
}

// Check if unit is an airborne target (Prefix check ensures DF-21D is NEVER classified as air)
bool IsAirUnit(const std::string& name) {
    if (name.find("Spirit") != std::string::npos ||
        name.find("Shahed-136") != std::string::npos ||
        name.find("AC-130") != std::string::npos ||
        name.find("A-10") != std::string::npos ||
        name.find("Su-") != std::string::npos ||
        name.find("Mig-") != std::string::npos ||
        name.find("Ka-") != std::string::npos ||
        name.find("Mi-") != std::string::npos ||
        name.find("AH-") != std::string::npos ||
        name.find("drone") != std::string::npos ||
        name.find("Drone") != std::string::npos ||
        name.find("Mirage") != std::string::npos ||
        StartsWith(name, "F-") ||
        StartsWith(name, "J-")) {
        return true;
    }
    return false;
}

// Check if unit is a stationary turret/launcher
bool IsTurretOrStationary(const std::string& name) {
    if (name == "ShahedLauncher" ||
        name == "PhalanxHD" ||
        name == "Kashtan" ||
        name == "SEA-RAM" ||
        name == "Goalkeeper" ||
        name == "AK630M-2-Duet" ||
        name == "Flak38_20mm" ||
        name.find("Zu-23-2") != std::string::npos) {
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Universal Spawn Execution on Unity Main Thread
// -----------------------------------------------------------------------------
void ExecuteSpawn(const SpawnRequest& req) {
    if (!il2cpp_string_new_fn || !GameAssetCatalog_GetUnitPrefab) {
        LOGE("[C-RAM-MOD] Spawn failed: il2cpp_string_new or GameAssetCatalog_GetUnitPrefab is null!");
        return;
    }

    std::string canonical = ResolveUnitAlias(req.rawName);
    LOGI("[C-RAM-MOD] Attempting to spawn: '%s' (raw: '%s', AI: %d, dist: %.0f, alt: %.0f)",
         canonical.c_str(), req.rawName.c_str(), (int)req.setupAI, req.dist, req.alt);

    void* strObj = il2cpp_string_new_fn(canonical.c_str());
    void* prefab = GameAssetCatalog_GetUnitPrefab(strObj, nullptr);
    if (!prefab && canonical != req.rawName) {
        strObj = il2cpp_string_new_fn(req.rawName.c_str());
        prefab = GameAssetCatalog_GetUnitPrefab(strObj, nullptr);
    }

    // Strict pointer validation: never crash on invalid/unloaded prefabs
    if (!prefab || !IsValidPtr(prefab)) {
        LOGE("[C-RAM-MOD] Prefab not found for '%s'!", canonical.c_str());
        return;
    }

    bool isAir = IsAirUnit(canonical);
    bool isTurret = IsTurretOrStationary(canonical);

    float defaultDist = isAir ? 400.0f : 25.0f;
    float defaultAlt = isAir ? 80.0f : 0.0f;

    float spawnDist = (req.dist >= 0.0f) ? req.dist : defaultDist;
    float spawnAlt = (req.alt >= 0.0f) ? req.alt : defaultAlt;

    Vector3 spawnPos(0, 0, 100);
    Quaternion spawnRot = Quaternion::Identity();

    void* cam = Camera_get_main ? Camera_get_main() : nullptr;
    if (cam && Component_get_transform && Transform_get_position) {
        void* camTr = Component_get_transform(cam);
        if (IsValidPtr(camTr)) {
            Vector3 cPos = Transform_get_position(camTr);

            Vector3 fwd(0, 0, 1);
            if (Transform_get_forward) {
                fwd = Transform_get_forward(camTr, nullptr);
            }

            if (req.hasCustomPos) {
                spawnPos = Vector3(req.customX, req.customY, req.customZ);
            } else if (isAir) {
                spawnPos = cPos + fwd * spawnDist + Vector3(0, spawnAlt, 0);
                spawnRot = Quaternion::LookRotation(fwd * -1.0f);
            } else {
                Vector3 groundFwd(fwd.X, 0.0f, fwd.Z);
                float len = Vector3::Magnitude(groundFwd);
                if (len > 0.001f) {
                    groundFwd = groundFwd / len;
                } else {
                    groundFwd = Vector3(0, 0, 1);
                }
                // Place vehicle in front of player on ground
                spawnPos = cPos + groundFwd * spawnDist;
                if (req.alt >= 0.0f) {
                    spawnPos.Y = req.alt;
                }
                // Face the vehicle towards the player
                spawnRot = Quaternion::LookRotation(groundFwd * -1.0f);
            }
        }
    }

    bool spawned = false;

    // 1. Static Turrets / Launchers (ShahedLauncher, Phalanx, Kashtan) -> Object.Instantiate on ground
    if (isTurret) {
        if (Object_Instantiate) {
            LOGI("[C-RAM-MOD] Spawning stationary launcher/turret via Object.Instantiate...");
            void* obj = Object_Instantiate(prefab, nullptr);
            if (obj && Component_get_transform && Transform_set_position) {
                void* tr = Component_get_transform(obj);
                if (tr) {
                    Transform_set_position(tr, spawnPos, nullptr);
                }
            }
            spawned = true;
        }
    }
    // 2. Air Units (Su-57, B-2, Shahed, AC-130, FPV, etc.)
    else if (isAir) {
        if (g_SpawnManagerInstance) {
            try {
                if (req.setupAI && SpawnManager_SpawnAirUnit) {
                    LOGI("[C-RAM-MOD] Spawning air unit with AI via SpawnManager::SpawnAirUnit...");
                    float roadH = (spawnAlt > 10.0f) ? spawnAlt : 350.0f;
                    SpawnManager_SpawnAirUnit(g_SpawnManagerInstance, prefab, spawnPos, spawnRot, nullptr, roadH, 7.0f, nullptr);
                    spawned = true;
                } else if (!req.setupAI && SpawnManager_SpawnUnit) {
                    LOGI("[C-RAM-MOD] Spawning air unit without AI (Dummy Target)...");
                    SpawnManager_SpawnUnit(g_SpawnManagerInstance, prefab, spawnPos, spawnRot, nullptr, false, nullptr);
                    spawned = true;
                }
            } catch (...) {
                LOGE("[C-RAM-MOD] Exception inside SpawnManager air call");
            }
        }
    }
    // 3. Ground Vehicles (DF-21D, Tor, Tunguska, Pantsir, Abrams, T-90, etc.)
    else {
        if (g_SpawnManagerInstance) {
            try {
                if (req.setupAI && SpawnManager_SpawnGroundUnit) {
                    LOGI("[C-RAM-MOD] Spawning ground unit with AI via SpawnManager::SpawnGroundUnit...");
                    SpawnManager_SpawnGroundUnit(g_SpawnManagerInstance, prefab, spawnPos, spawnRot, nullptr, nullptr);
                    spawned = true;
                } else if (SpawnManager_SpawnUnit) {
                    LOGI("[C-RAM-MOD] Spawning ground unit without AI (setupAI=0)...");
                    void* unit = SpawnManager_SpawnUnit(g_SpawnManagerInstance, prefab, spawnPos, spawnRot, nullptr, false, nullptr);
                    if (unit && IsValidPtr(unit) && IUnit_GetUnitType && SpawnManager_SnapGroundUnitToSurface) {
                        try {
                            if (IUnit_GetUnitType(unit) == 1) {
                                SpawnManager_SnapGroundUnitToSurface(g_SpawnManagerInstance, unit, nullptr);
                            }
                        } catch (...) {
                            LOGE("[C-RAM-MOD] Exception in SnapGroundUnitToSurface");
                        }
                    }
                    spawned = true;
                }
            } catch (...) {
                LOGE("[C-RAM-MOD] Exception inside SpawnManager ground call");
            }
        }
    }

    // Generic fallback if not handled
    if (!spawned && Object_Instantiate) {
        LOGI("[C-RAM-MOD] Spawning via Object.Instantiate fallback...");
        void* obj = Object_Instantiate(prefab, nullptr);
        if (obj && Component_get_transform && Transform_set_position) {
            void* tr = Component_get_transform(obj);
            if (tr) {
                Transform_set_position(tr, spawnPos, nullptr);
            }
        }
        spawned = true;
    }

    LOGI("[C-RAM-MOD] Spawned '%s' successfully! spawned=%d at (%.1f, %.1f, %.1f)",
         canonical.c_str(), (int)spawned, spawnPos.X, spawnPos.Y, spawnPos.Z);
}

// -----------------------------------------------------------------------------
// Hook on SpawnManager.Awake (Captures active battle spawner)
// -----------------------------------------------------------------------------
void hook_SpawnManager_Awake(void* self) {
    g_SpawnManagerInstance = self;
    LOGI("[C-RAM-MOD] SpawnManager::Awake captured! Instance: %p", self);
    if (orig_SpawnManager_Awake) {
        orig_SpawnManager_Awake(self);
    }
}

// -----------------------------------------------------------------------------
// Hook on Diamond Button (IAPShopButton.OnEnable & IconsPanel.ShowStore)
// -----------------------------------------------------------------------------
void hook_IAPShopButton_OnEnable(void* self) {
    LOGI("[C-RAM-MOD] Diamond Icon pressed -> IAPShopButton::OnEnable intercepted!");
    uint64_t now = getNowMs();
    if (now - g_LastTriggerTime > 500) {
        g_LastTriggerTime = now;
        g_OpenMultiplayerRequested.store(true);
    }
}

void hook_IconsPanel_ShowStore(void* self) {
    LOGI("[C-RAM-MOD] IconsPanel::ShowStore intercepted!");
    uint64_t now = getNowMs();
    if (now - g_LastTriggerTime > 500) {
        g_LastTriggerTime = now;
        g_OpenMultiplayerRequested.store(true);
    }
}

// -----------------------------------------------------------------------------
// Hook on EventSystem.Update (Unity Main Thread Handler)
// -----------------------------------------------------------------------------
void hook_EventSystem_Update(void* self) {
    if (orig_EventSystem_Update) {
        orig_EventSystem_Update(self);
    }

    // 1. Multiplayer Launch Trigger
    if (g_OpenMultiplayerRequested.load()) {
        g_OpenMultiplayerRequested.store(false);
        LOGI("[C-RAM-MOD] Launching Hidden Multiplayer on Unity Main Thread!");

        if (MpBootstrap) {
            MpBootstrap();
        }

        if (MpEntry_OnClicked) {
            MpEntry_OnClicked(self);
        }

        LOGI("[C-RAM-MOD] Multiplayer Screen Launched Successfully!");
    }

    // 2. Unit Spawning Trigger
    if (g_SpawnRequested.load()) {
        g_SpawnRequested.store(false);

        std::vector<SpawnRequest> toSpawn;
        {
            std::lock_guard<std::mutex> lock(g_SpawnMutex);
            toSpawn.swap(g_PendingSpawnQueue);
        }

        for (const auto& req : toSpawn) {
            ExecuteSpawn(req);
        }
    }
}

// -----------------------------------------------------------------------------
// TCP Command Server (Runs on background thread, listening on 0.0.0.0:8888)
// -----------------------------------------------------------------------------
void* socket_server_thread(void*) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOGE("[C-RAM-MOD] Failed to create socket");
        return nullptr;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0 (Accessible via ADB and Wi-Fi)
    address.sin_port = htons(8888);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        LOGE("[C-RAM-MOD] Failed to bind to port 8888");
        close(server_fd);
        return nullptr;
    }

    if (listen(server_fd, 5) < 0) {
        LOGE("[C-RAM-MOD] Failed to listen on socket");
        close(server_fd);
        return nullptr;
    }

    LOGI("[C-RAM-MOD] TCP Command Server ready on 0.0.0.0:8888 (use: adb forward tcp:8888 tcp:8888)");

    char buf[512];
    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            memset(buf, 0, sizeof(buf));
            int n = read(client_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                // Trim trailing whitespaces / newlines
                while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) {
                    buf[--n] = '\0';
                }

                LOGI("[C-RAM-MOD] TCP Command received: '%s'", buf);

                char reply[512];
                if (strcasecmp(buf, "ping") == 0) {
                    snprintf(reply, sizeof(reply), "PONG: C-RAM Mod v4.5 Online\n");
                } else if (strcasecmp(buf, "help") == 0) {
                    snprintf(reply, sizeof(reply),
                             "Commands:\n"
                             "  ping\n"
                             "  spawn <unit> [noai] [dist <meters>] [alt <meters>]\n"
                             "  list\n"
                             "  mp\n");
                } else if (strcasecmp(buf, "list") == 0) {
                    snprintf(reply, sizeof(reply),
                             "Units Available:\n"
                             "  Ground: df-21d, tor, tunguska, pantsir, buk, patriot, shilka, abrams, t90, t72\n"
                             "  Air: su57, b2, shahed, ac130, su25, su27, mig29, f14, f15, f16, f18, f35, fpv\n"
                             "  Heli: ka52, mi28, mi24, apache, cobra\n"
                             "  Turrets: shahedlauncher, phalanx, kashtan, searam, goalkeeper\n");
                } else if (strncasecmp(buf, "spawn", 5) == 0) {
                    char* args = buf + 5;
                    while (*args == ' ') args++;
                    if (*args != '\0') {
                        SpawnRequest req;
                        req.hasCustomPos = false;
                        req.dist = -1.0f;
                        req.alt = -1.0f;
                        req.setupAI = true;

                        std::vector<std::string> tokens;
                        char temp[256];
                        strncpy(temp, args, sizeof(temp) - 1);
                        temp[sizeof(temp) - 1] = '\0';
                        char* p = strtok(temp, " \t\r\n");
                        while (p) {
                            tokens.push_back(p);
                            p = strtok(nullptr, " \t\r\n");
                        }

                        std::string unitStr = "";
                        for (size_t ti = 0; ti < tokens.size(); ti++) {
                            std::string t = tokens[ti];
                            std::string tlower = t;
                            for (char& c : tlower) c = tolower((unsigned char)c);

                            if (tlower == "noai" || tlower == "--noai") {
                                req.setupAI = false;
                            } else if ((tlower == "dist" || tlower == "--dist" || tlower == "-d") && ti + 1 < tokens.size()) {
                                req.dist = (float)atof(tokens[++ti].c_str());
                            } else if ((tlower == "alt" || tlower == "--alt" || tlower == "-a") && ti + 1 < tokens.size()) {
                                req.alt = (float)atof(tokens[++ti].c_str());
                            } else if (ti + 2 < tokens.size() &&
                                       (isdigit(t[0]) || t[0] == '-') &&
                                       (isdigit(tokens[ti+1][0]) || tokens[ti+1][0] == '-') &&
                                       (isdigit(tokens[ti+2][0]) || tokens[ti+2][0] == '-')) {
                                req.hasCustomPos = true;
                                req.customX = (float)atof(t.c_str());
                                req.customY = (float)atof(tokens[++ti].c_str());
                                req.customZ = (float)atof(tokens[++ti].c_str());
                            } else {
                                if (!unitStr.empty()) unitStr += " ";
                                unitStr += t;
                            }
                        }

                        if (unitStr.empty()) unitStr = "su57";
                        req.rawName = unitStr;

                        {
                            std::lock_guard<std::mutex> lock(g_SpawnMutex);
                            g_PendingSpawnQueue.push_back(req);
                            g_SpawnRequested.store(true);
                        }

                        snprintf(reply, sizeof(reply), "OK: Spawn '%s' queued (AI: %s, dist: %.0f, alt: %.0f)\n",
                                 req.rawName.c_str(), req.setupAI ? "ON" : "OFF (Dummy)",
                                 req.dist, req.alt);
                    } else {
                        snprintf(reply, sizeof(reply), "ERROR: Usage: spawn <Unit> [noai] [dist <meters>] [alt <meters>]\n");
                    }
                } else if (strcasecmp(buf, "mp") == 0) {
                    g_OpenMultiplayerRequested.store(true);
                    snprintf(reply, sizeof(reply), "OK: Multiplayer Triggered\n");
                } else {
                    snprintf(reply, sizeof(reply), "UNKNOWN COMMAND: '%s'. Type 'help' or 'list'\n", buf);
                }

                write(client_fd, reply, strlen(reply));
            }
            close(client_fd);
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Lead Math Solver (Exact Quadratic Ballistic Solver)
// -----------------------------------------------------------------------------
static bool CalculateLead(
    const Vector3& targetPos,
    const Vector3& targetVel,
    const Vector3& gunPos,
    float bulletSpeed,
    bool noGravity,
    Vector3& outLeadPos,
    float& outFlightTime,
    float& outDistance
) {
    Vector3 rel = targetPos - gunPos;
    outDistance = Vector3::Magnitude(rel);
    if (outDistance < 2.0f || outDistance > 7000.0f || bulletSpeed <= 10.0f) {
        return false;
    }

    float vSqr = Vector3::Dot(targetVel, targetVel);
    float bSqr = bulletSpeed * bulletSpeed;
    float a = vSqr - bSqr;
    float b = 2.0f * Vector3::Dot(rel, targetVel);
    float c = Vector3::Dot(rel, rel);

    float t = -1.0f;
    float D = b * b - 4.0f * a * c;

    if (D >= 0.0f) {
        float sqrtD = sqrtf(D);
        float t1 = (-b - sqrtD) / (2.0f * a);
        float t2 = (-b + sqrtD) / (2.0f * a);

        if (t1 > 0.001f && t2 > 0.001f) {
            t = fminf(t1, t2);
        } else if (t1 > 0.001f) {
            t = t1;
        } else if (t2 > 0.001f) {
            t = t2;
        }
    }

    if (t <= 0.0f) {
        t = outDistance / bulletSpeed;
    }

    outFlightTime = t;
    outLeadPos = targetPos + targetVel * t;

    // Ballistic drop compensation (game gravity = 9.0)
    if (!noGravity) {
        outLeadPos.Y += 0.5f * 9.0f * t * t;
    }

    return true;
}

// -----------------------------------------------------------------------------
// Battle Hook (GeneralHUD.LateUpdate)
// -----------------------------------------------------------------------------
void hook_GeneralHUD_LateUpdate(void* self) {
    if (orig_GeneralHUD_LateUpdate) {
        orig_GeneralHUD_LateUpdate(self);
    }

    if (!IsValidPtr(self) || !Camera_WorldToScreenPoint || !Camera_get_main) {
        return;
    }

    void* cam = *(void**)((uintptr_t)self + OFFSET_HUD_MAINCAMERA);
    if (!IsValidPtr(cam)) {
        cam = Camera_get_main();
    }
    if (!IsValidPtr(cam)) return;

    Vector3 gunPos(0, 0, 0);
    float bulletSpeed = 1050.0f;
    bool noGravity = false;
    bool foundTurret = false;

    void* seat = *(void**)((uintptr_t)self + OFFSET_HUD_PARENTSEAT);
    if (IsValidPtr(seat)) {
        void* aws = *(void**)((uintptr_t)seat + OFFSET_SEAT_AWS);
        if (IsValidPtr(aws)) {
            void* activeTurret = nullptr;

            auto selectable = *(monoList<void*>**)((uintptr_t)aws + OFFSET_AWS_SELECTABLE);
            if (IsValidPtr(selectable) && IsValidPtr(selectable->items) && selectable->getSize() > 0) {
                activeTurret = ((void**)selectable->items->vector)[0];
            }

            if (!IsValidPtr(activeTurret)) {
                auto alwaysReady = *(monoList<void*>**)((uintptr_t)aws + OFFSET_AWS_ALWAYSREADY);
                if (IsValidPtr(alwaysReady) && IsValidPtr(alwaysReady->items) && alwaysReady->getSize() > 0) {
                    activeTurret = ((void**)alwaysReady->items->vector)[0];
                }
            }

            if (IsValidPtr(activeTurret)) {
                foundTurret = true;
                void* muzzle = *(void**)((uintptr_t)activeTurret + OFFSET_TURRET_MUZZLE);
                if (IsValidPtr(muzzle) && Component_get_transform && Transform_get_position) {
                    void* muzzleTr = Component_get_transform(muzzle);
                    if (IsValidPtr(muzzleTr)) {
                        gunPos = Transform_get_position(muzzleTr);
                    }
                }

                void* ammoConfig = *(void**)((uintptr_t)activeTurret + OFFSET_TURRET_AMMOCONFIG);
                if (IsValidPtr(ammoConfig)) {
                    float spd = *(float*)((uintptr_t)ammoConfig + OFFSET_AMMO_SPEED);
                    if (spd > 50.0f) bulletSpeed = spd;
                }

                noGravity = *(bool*)((uintptr_t)activeTurret + OFFSET_TURRET_NOGRAVITY);
            }
        }
    }

    if (!foundTurret && Component_get_transform && Transform_get_position) {
        void* camTr = Component_get_transform(cam);
        if (IsValidPtr(camTr)) {
            gunPos = Transform_get_position(camTr);
        }
    }

    auto enemyList = *(monoList<void*>**)((uintptr_t)self + OFFSET_HUD_ALLENEMIES);
    if (!IsValidPtr(enemyList) || !IsValidPtr(enemyList->items)) return;

    int enemyCount = enemyList->getSize();
    if (enemyCount <= 0 || enemyCount > 256) return;

    int writeIdx = 1 - g_ActiveBufferIdx.load();
    int count = 0;
    float screenHeight = (glHeight > 0) ? (float)glHeight : (float)ImGui::GetIO().DisplaySize.y;
    if (screenHeight <= 0.0f) screenHeight = 1080.0f;

    for (int i = 0; i < enemyCount && count < MAX_TARGETS; i++) {
        void* target = ((void**)enemyList->items->vector)[i];
        if (!IsValidPtr(target)) continue;

        bool isAlive = *(bool*)((uintptr_t)target + OFFSET_UNIT_ISALIVE);
        if (!isAlive) continue;

        if (IUnit_GetUnitType) {
            int unitType = IUnit_GetUnitType(target);
            if (unitType != 3 && unitType != 6 && unitType != 7 && unitType != 5) {
                continue;
            }
        }

        void* tr = Component_get_transform ? Component_get_transform(target) : nullptr;
        if (!IsValidPtr(tr) || !Transform_get_position) continue;
        Vector3 targetPos = Transform_get_position(tr);

        Vector3 targetVel(0, 0, 0);
        if (PhysicsObject_get_Velocity) {
            targetVel = PhysicsObject_get_Velocity(target);
        }

        Vector3 leadPos(0, 0, 0);
        float flightTime = 0.0f;
        float distance = 0.0f;

        if (!CalculateLead(targetPos, targetVel, gunPos, bulletSpeed, noGravity, leadPos, flightTime, distance)) {
            continue;
        }

        Vector3 leadScreen = Camera_WorldToScreenPoint(cam, leadPos);
        if (leadScreen.Z <= 0.0f) continue;

        Vector3 targetScreen = Camera_WorldToScreenPoint(cam, targetPos);

        ScreenLeadTarget& entry = g_TargetsBuffer[writeIdx][count];
        entry.leadX = leadScreen.X;
        entry.leadY = screenHeight - leadScreen.Y;
        entry.distance = distance;
        entry.flightTime = flightTime;

        if (targetScreen.Z > 0.0f) {
            entry.hasTargetLine = true;
            entry.targetX = targetScreen.X;
            entry.targetY = screenHeight - targetScreen.Y;
        } else {
            entry.hasTargetLine = false;
        }

        count++;
    }

    g_TargetsCount[writeIdx] = count;
    g_ActiveBufferIdx.store(writeIdx);
}

// -----------------------------------------------------------------------------
// ESP Drawing Loop (Render Thread inside eglSwapBuffers)
// -----------------------------------------------------------------------------
void DrawMenu() {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw) return;

    int readIdx = g_ActiveBufferIdx.load();
    int count = g_TargetsCount[readIdx];
    if (count <= 0) return;

    const float leadCircleRadius = 14.0f;
    const ImU32 circleColor = IM_COL32(255, 45, 45, 240);
    const ImU32 centerColor = IM_COL32(255, 255, 255, 255);
    const ImU32 lineColor = IM_COL32(255, 210, 50, 170);
    const ImU32 textColor = IM_COL32(255, 255, 255, 240);

    for (int i = 0; i < count; i++) {
        const ScreenLeadTarget& tgt = g_TargetsBuffer[readIdx][i];
        ImVec2 leadPoint(tgt.leadX, tgt.leadY);

        draw->AddCircle(leadPoint, leadCircleRadius, circleColor, 24, 2.5f);
        draw->AddCircleFilled(leadPoint, 2.5f, centerColor);

        float cross = leadCircleRadius * 0.5f;
        draw->AddLine(ImVec2(leadPoint.x - cross, leadPoint.y), ImVec2(leadPoint.x + cross, leadPoint.y), circleColor, 1.5f);
        draw->AddLine(ImVec2(leadPoint.x, leadPoint.y - cross), ImVec2(leadPoint.x, leadPoint.y + cross), circleColor, 1.5f);

        if (tgt.hasTargetLine) {
            draw->AddLine(ImVec2(tgt.targetX, tgt.targetY), leadPoint, lineColor, 1.5f);
        }

        char textBuf[64];
        snprintf(textBuf, sizeof(textBuf), "%.0fm (%.1fs)", tgt.distance, tgt.flightTime);
        draw->AddText(ImVec2(leadPoint.x + leadCircleRadius + 5, leadPoint.y - 8), textColor, textBuf);
    }
}

// -----------------------------------------------------------------------------
// Helper to locate libil2cpp.so across Android linking modes
// -----------------------------------------------------------------------------
struct Il2CppSearch {
    uintptr_t base;
    char foundPath[512];
};

static int phdr_callback(struct dl_phdr_info* info, size_t size, void* data) {
    Il2CppSearch* s = (Il2CppSearch*)data;
    if (info->dlpi_name && (strstr(info->dlpi_name, "libil2cpp.so") || strstr(info->dlpi_name, "il2cpp"))) {
        s->base = (uintptr_t)info->dlpi_addr;
        strncpy(s->foundPath, info->dlpi_name, sizeof(s->foundPath) - 1);
        return 1;
    }
    return 0;
}

static uintptr_t getIl2CppBaseAddress() {
    Il2CppSearch s = {0, {0}};
    dl_iterate_phdr(phdr_callback, &s);
    if (s.base != 0) {
        LOGI("Found libil2cpp via dl_iterate_phdr: %p (%s)", (void*)s.base, s.foundPath);
        return s.base;
    }

    const char* testSyms[] = {"il2cpp_init", "il2cpp_domain_get", "il2cpp_thread_attach", "il2cpp_runtime_invoke"};
    for (const char* symName : testSyms) {
        void* sym = dlsym(RTLD_DEFAULT, symName);
        if (sym) {
            Dl_info info;
            if (dladdr(sym, &info) && info.dli_fbase) {
                LOGI("Found libil2cpp via RTLD_DEFAULT dlsym(%s)+dladdr: %p (%s)",
                     symName, info.dli_fbase, info.dli_fname ? info.dli_fname : "");
                return (uintptr_t)info.dli_fbase;
            }
        }
    }

    void* h = dlopen("libil2cpp.so", RTLD_NOLOAD);
    if (!h) h = dlopen("libil2cpp.so", RTLD_LAZY);
    if (h) {
        for (const char* symName : testSyms) {
            void* sym = dlsym(h, symName);
            if (sym) {
                Dl_info info;
                if (dladdr(sym, &info) && info.dli_fbase) {
                    LOGI("Found libil2cpp via dlopen dlsym(%s)+dladdr: %p (%s)",
                         symName, info.dli_fbase, info.dli_fname ? info.dli_fname : "");
                    return (uintptr_t)info.dli_fbase;
                }
            }
        }
    }

    uintptr_t mapsBase = getBaseAddress("libil2cpp.so");
    if (mapsBase != 0) {
        LOGI("Found libil2cpp via /proc/self/maps: %p", (void*)mapsBase);
        return mapsBase;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Hook Initialization Thread
// -----------------------------------------------------------------------------
void* thread(void*) {
    LOGI("C-RAM Mod Thread Started (v4.5 - Ground & DF-21D Fixed)");

    initModMenu((void*)DrawMenu);

    int waitCounter = 0;
    while (!g_Il2CppBase) {
        g_Il2CppBase = getIl2CppBaseAddress();
        if (!g_Il2CppBase) {
            if (waitCounter % 10 == 0) {
                LOGI("Waiting for libil2cpp.so... (%d attempts)", waitCounter);
            }
            waitCounter++;
            usleep(200000);
        }
    }

    LOGI("libil2cpp.so found at: %p", (void*)g_Il2CppBase);
    sleep(1);

    // Battle pointers
    Camera_get_main = (t_Camera_get_main)(g_Il2CppBase + RVA_CAMERA_MAIN);
    Camera_WorldToScreenPoint = (t_Camera_WorldToScreenPoint)(g_Il2CppBase + RVA_CAMERA_W2S);
    Component_get_transform = (t_Component_get_transform)(g_Il2CppBase + RVA_COMP_TRANSFORM);
    Transform_get_position = (t_Transform_get_position)(g_Il2CppBase + RVA_TRANS_POS);
    Transform_set_position = (t_Transform_set_position)(g_Il2CppBase + RVA_TRANS_SETPOS);
    Transform_get_forward = (t_Transform_get_forward)(g_Il2CppBase + RVA_TRANS_GETFORWARD);
    IUnit_GetUnitType = (t_IUnit_GetUnitType)(g_Il2CppBase + RVA_UNIT_TYPE);
    PhysicsObject_get_Velocity = (t_PhysicsObject_get_Velocity)(g_Il2CppBase + RVA_PHYS_VEL);

    // Multiplayer pointers
    MpBootstrap = (t_MpBootstrap)(g_Il2CppBase + RVA_MP_BOOTSTRAP);
    MpEntry_OnClicked = (t_MpEntry_OnClicked)(g_Il2CppBase + RVA_MP_ENTRY_CLICKED);

    // Spawning pointers
    il2cpp_string_new_fn = (t_il2cpp_string_new)dlsym(RTLD_DEFAULT, "il2cpp_string_new");
    if (!il2cpp_string_new_fn) {
        void* h = dlopen("libil2cpp.so", RTLD_NOLOAD);
        if (h) il2cpp_string_new_fn = (t_il2cpp_string_new)dlsym(h, "il2cpp_string_new");
    }
    GameAssetCatalog_GetUnitPrefab = (t_GameAssetCatalog_GetUnitPrefab)(g_Il2CppBase + RVA_ASSET_CATALOG_GET_PREFAB);
    SpawnManager_SpawnUnit = (t_SpawnManager_SpawnUnit)(g_Il2CppBase + RVA_SPAWN_MANAGER_SPAWN_UNIT);
    SpawnManager_SpawnAirUnit = (t_SpawnManager_SpawnAirUnit)(g_Il2CppBase + RVA_SPAWN_MANAGER_SPAWN_AIR);
    SpawnManager_SpawnGroundUnit = (t_SpawnManager_SpawnGroundUnit)(g_Il2CppBase + RVA_SPAWN_MANAGER_SPAWN_GROUND);
    SpawnManager_SnapGroundUnitToSurface = (t_SpawnManager_SnapGroundUnitToSurface)(g_Il2CppBase + RVA_SPAWN_MANAGER_SNAP_GROUND);
    Object_Instantiate = (t_Object_Instantiate)(g_Il2CppBase + RVA_OBJECT_INSTANTIATE);

    // 1. Hook GeneralHUD.LateUpdate for Battle ESP (Lead Indicator)
    void* targetHUDMethod = (void*)(g_Il2CppBase + RVA_HUD_LATEUPDATE);
    DobbyHook(targetHUDMethod, (void*)hook_GeneralHUD_LateUpdate, (void**)&orig_GeneralHUD_LateUpdate);

    // 2. Hook EventSystem.Update for safe execution on Unity Main Thread
    void* targetEventSystemMethod = (void*)(g_Il2CppBase + RVA_EVENTSYSTEM_UPDATE);
    DobbyHook(targetEventSystemMethod, (void*)hook_EventSystem_Update, (void**)&orig_EventSystem_Update);

    // 3. Hook SpawnManager.Awake to capture active battle spawner
    void* targetSpawnManagerAwake = (void*)(g_Il2CppBase + RVA_SPAWN_MANAGER_AWAKE);
    DobbyHook(targetSpawnManagerAwake, (void*)hook_SpawnManager_Awake, (void**)&orig_SpawnManager_Awake);

    // 4. Hook IAPShopButton.OnEnable & IconsPanel.ShowStore (Diamond icon in hangar header)
    void* targetIAPShop = (void*)(g_Il2CppBase + RVA_IAP_SHOP_ONENABLE);
    DobbyHook(targetIAPShop, (void*)hook_IAPShopButton_OnEnable, (void**)&orig_IAPShopButton_OnEnable);

    void* targetShowStore = (void*)(g_Il2CppBase + RVA_ICONS_SHOWSTORE);
    DobbyHook(targetShowStore, (void*)hook_IconsPanel_ShowStore, (void**)&orig_IconsPanel_ShowStore);

    // 5. Component Guards: Eliminate NullReferenceExceptions on Ground Units & DF-21D
    void* targetTrackDeformer = (void*)(g_Il2CppBase + RVA_TRACK_DEFORMER_UPDATE);
    DobbyHook(targetTrackDeformer, (void*)hook_TrackDeformer_Update, (void**)&orig_TrackDeformer_Update);

    void* targetRotatingSensor = (void*)(g_Il2CppBase + RVA_ROTATING_SENSOR_UPDATE);
    DobbyHook(targetRotatingSensor, (void*)hook_RotatingSensor_Update, (void**)&orig_RotatingSensor_Update);

    // 6. Start ADB Socket Listener Thread on 0.0.0.0:8888
    pthread_t sock_t;
    pthread_create(&sock_t, nullptr, socket_server_thread, nullptr);

    LOGI("C-RAM v4.5 Hooks Installed: Lead ESP + Crash Guards + Camera Forward Aiming + TCP Server Ready!");
    pthread_exit(nullptr);
}

// JNI Support
extern "C" {
    JavaVM* jvm = nullptr;
    JNIEnv* env = nullptr;

    __attribute__((visibility("default")))
    jint loadJNI(JavaVM* vm) {
        jvm = vm;
        vm->AttachCurrentThread(&env, nullptr);
        return JNI_VERSION_1_6;
    }
}

// Auto-run when .so is loaded
__attribute__((constructor))
void init() {
    pthread_t t;
    pthread_create(&t, nullptr, thread, nullptr);
}