//
// C-RAM Mod v6.0: Cockpit Cursor Steering + Multi-Stage Combat Lead Stabilizer (Anti-Jitter Edition)
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
// RVAs from script.json (Relative to libil2cpp.so base) - Verified for Update
// -----------------------------------------------------------------------------
#define RVA_CAMERA_MAIN                 0x7601668 // UnityEngine.Camera.get_main
#define RVA_CAMERA_W2S                  0x7600BBC // UnityEngine.Camera.WorldToScreenPoint
#define RVA_COMP_TRANSFORM              0x7675B08 // UnityEngine.Component.get_transform
#define RVA_GAMEOBJECT_TRANSFORM        0x767AAD4 // UnityEngine.GameObject.get_transform
#define RVA_TRANS_POS                   0x768C0D8 // UnityEngine.Transform.get_position
#define RVA_TRANS_SETPOS                0x768C1AC // UnityEngine.Transform.set_position
#define RVA_TRANS_GETFORWARD            0x768CB34 // UnityEngine.Transform.get_forward
#define RVA_UNIT_TYPE                   0x3B38DB4 // IUnit.GetUnitType
#define RVA_PHYS_VEL                    0x39DEC94 // PhysicsObject.get_Velocity
#define RVA_HUD_LATEUPDATE              0x3ACF27C // GeneralHUD.LateUpdate

// Multiplayer & UI Hooks
#define RVA_EVENTSYSTEM_UPDATE          0x79EC8B4 // UnityEngine.EventSystems.EventSystem.Update
#define RVA_MP_BOOTSTRAP                0x397F9A8 // MultiplayerService.Bootstrap
#define RVA_MP_ENTRY_CLICKED            0x3979048 // MultiplayerEntryButton.OnClicked

// Button / Shop Hooks
#define RVA_IAP_SHOP_ONENABLE           0x3909188 // IAPShopButton.OnEnable (Diamond icon in hangar)
#define RVA_ICONS_SHOWSTORE             0x3E46C1C // CBS.UI.IconsPanel.ShowStore (Backup)

// Unit Spawning & Catalog
#define RVA_ASSET_CATALOG_GET_PREFAB    0x38EE1D0 // GameAssetCatalog.GetUnitPrefab(string idOrName)
#define RVA_SPAWN_MANAGER_AWAKE         0x3A1B264 // SpawnManager.Awake
#define RVA_SPAWN_MANAGER_SPAWN_UNIT    0x3A1EA08 // SpawnManager.SpawnUnit
#define RVA_SPAWN_MANAGER_SPAWN_AIR     0x3A1EFDC // SpawnManager.SpawnAirUnit
#define RVA_SPAWN_MANAGER_SPAWN_GROUND  0x3A1F40C // SpawnManager.SpawnGroundUnit
#define RVA_SPAWN_MANAGER_SNAP_GROUND   0x3A1F5C8 // SpawnManager.SnapGroundUnitToSurface
#define RVA_OBJECT_INSTANTIATE          0x7681AD4 // UnityEngine.Object.Instantiate(Object original)

// Ground Component Fixes
#define RVA_TRACK_DEFORMER_UPDATE       0x3B50130 // TrackDeformer.Update
#define RVA_ROTATING_SENSOR_UPDATE      0x3B8A240 // RotatingSensor.Update

// CameraManager & Cockpit Cursor Steering Hooks
#define RVA_CAMERAMANAGER_AWAKE         0x3EFA604 // CameraManager.Awake
#define RVA_CAMERAMANAGER_SETCAMERABYTYPE 0x3EF815C // CameraManager.SetCameraByType(GameCameraType type)
#define RVA_CAMERAMANAGER_SETCAMERA     0x3EFB430 // CameraManager.SetCamera(GameCameraType gameCameraType, VehicleSeat follow, UnitType unitType)
#define RVA_VEHICLESEAT_INITIALIZEHUD   0x3B48D04 // VehicleSeat.InitializeHUD
#define RVA_AIRPLANE_FIXEDUPDATE        0x3BB5FDC // Airplane.FixedUpdate
#define RVA_AIRCRAFTMIO_SETCONTROLLER   0x3B26688 // AircraftMio.SetAirCraftControllerType(AircraftControllerType type)
#define RVA_JOYSTICK_START              0x38976BC // Joystick.Start
#define RVA_ULTIMATEJOYSTICK_DISABLE    0x3C85A7C // UltimateJoystick.DisableJoystick(string joystickName)
#define RVA_GAMEOBJECT_SETACTIVE        0x767AF68 // UnityEngine.GameObject.SetActive(bool value)
#define RVA_COMP_GET_GAMEOBJECT         0x7675CAC // UnityEngine.Component.get_gameObject

// Economy / Give Currency Hooks
#define RVA_INVENTORY_AWAKE             0x38EAB78 // InventoryWrapper.Awake
#define RVA_INVENTORY_ADDCURRENCY       0x38EB6C4 // InventoryWrapper.AddCurrency(int amount)

// Landing Gear & Screen/Input RVAs
#define RVA_AIRCRAFTMIO_TOGGLEGEAR      0x3B24E7C // AircraftMio.ToggleLandingGear
#define RVA_HELICOPTERMIO_TOGGLEGEAR    0x3B3559C // HelicopterMio.ToggleLandingGear
#define RVA_STAGEUI_ONLANDINGGEAR       0x3B7CB54 // StageUI.OnLandingGearButtonClicked
#define RVA_AIRCRAFT_SETGEAR            0x3EDE5AC // MGAssets.AircraftPhysics.Aircraft.setGear
#define RVA_AIRCRAFT_SETGEARFORREPLAY   0x3EDEA28 // Aircraft.SetGearForReplay(bool down)
#define RVA_BUTTON_SET_IS_ON            0x3B7DE08 // ToggleOrHoldButton.set_IsOn(bool value)
#define RVA_BUTTON_ONPOINTERDOWN        0x3B7E4D8 // ToggleOrHoldButton.OnPointerDown
#define RVA_BUTTON_ONPOINTERUP          0x3B7E458 // ToggleOrHoldButton.OnPointerUp
#define RVA_BUTTON_FLASH                0x3B7E560 // ToggleOrHoldButton.Flash
#define RVA_BEHAVIOUR_GET_ENABLED       0x7675248 // UnityEngine.Behaviour.get_enabled
#define RVA_OBJECT_INSTANTIATE_PARENT   0x7682438  // UnityEngine.Object.Instantiate(Object original, Transform parent, bool instantiateInWorldSpace)
#define RVA_INPUT_GETMOUSEBUTTONDOWN    0x7704724 // UnityEngine.Input.GetMouseButtonDown
#define RVA_INPUT_GETMOUSEPOS_INJ       0x7704D94 // UnityEngine.Input.get_mousePosition_Injected
#define RVA_SCREEN_GETWIDTH             0x7614088 // UnityEngine.Screen.get_width
#define RVA_SCREEN_GETHEIGHT            0x76140B0 // UnityEngine.Screen.get_height

// AC-130 & Turret Stabilization RVAs
#define RVA_TURRET_FIXEDUPDATE          0x3B41EAC // Turret.FixedUpdate
#define RVA_CAMERA_SHAKE_GUNFIRE        0x3F019FC // CameraShakeManager.GunfireAt

// -----------------------------------------------------------------------------
// Field Offsets from dump.cs & il2cpp.h (Updated for latest game version)
// -----------------------------------------------------------------------------
#define OFFSET_HUD_PARENTSEAT           0x20
#define OFFSET_HUD_MAINCAMERA           0xE8
#define OFFSET_HUD_ALLENEMIES           0x140

#define OFFSET_SEAT_AWS                 0x28
#define OFFSET_SEAT_OBJECTS_POV_ONLY    0x70 // monoList<void*>

#define OFFSET_AWS_ALWAYSREADY          0x28
#define OFFSET_AWS_SELECTABLE           0x98 // Updated from 0x80 -> 0x98

#define OFFSET_TURRET_MUZZLE            0xD8
#define OFFSET_TURRET_AMMOCONFIG        0xC0
#define OFFSET_TURRET_NOGRAVITY         0xFC

#define OFFSET_AMMO_SPEED               0x24

#define OFFSET_UNIT_ISALIVE             0xEC // Updated from 0xC4 -> 0xEC

// Airplane & AircraftMio Offsets
#define OFFSET_AIRPLANE_AIMRETICLE      0xB8
#define OFFSET_AIRPLANE_CURSOR          0xC0
#define OFFSET_AIRPLANE_CURSORSTEERING  0x100
#define OFFSET_AIRPLANE_PARENT          0x30

#define OFFSET_MIO_AIRPLANE             0x1A0
#define OFFSET_MIO_CONTROLLERTYPE       0x1F0

// -----------------------------------------------------------------------------
// Function Pointer Types
// -----------------------------------------------------------------------------
typedef Vector3 (*t_Camera_WorldToScreenPoint)(void* camera, Vector3 position);
typedef void* (*t_Camera_get_main)();
typedef void* (*t_Component_get_transform)(void* component);
typedef void* (*t_GameObject_get_transform)(void* gameObject);
typedef Vector3 (*t_Transform_get_position)(void* transform);
typedef void (*t_Transform_set_position)(void* transform, Vector3 position, void* method);
typedef Vector3 (*t_Transform_get_forward)(void* transform, void* method);
typedef int (*t_IUnit_GetUnitType)(void* unit);
typedef Vector3 (*t_PhysicsObject_get_Velocity)(void* physicsObject);
typedef void (*t_GeneralHUD_LateUpdate)(void* self);
typedef void (*t_Turret_FixedUpdate)(void* self);
typedef void (*t_CameraShakeManager_GunfireAt)(Vector3 position, float magnitude);

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

// Cockpit Steering & UI pointers
typedef void (*t_CameraManager_Awake)(void* self, void* method);
typedef void (*t_CameraManager_SetCameraByType)(void* self, int type, void* method);
typedef void (*t_CameraManager_SetCamera)(void* self, int gameCameraType, void* follow, int unitType, void* method);
typedef void (*t_VehicleSeat_InitializeHUD)(void* self);
typedef void (*t_Airplane_FixedUpdate)(void* self, void* method);
typedef void (*t_AircraftMio_SetAirCraftControllerType)(void* self, int type, void* method);
typedef void (*t_Joystick_Start)(void* self, void* method);
typedef void (*t_UltimateJoystick_DisableJoystick)(void* joystickName, void* method);
typedef void (*t_GameObject_SetActive)(void* self, bool value, void* method);
typedef void* (*t_Component_get_gameObject)(void* self, void* method);

// Economy pointers
typedef void (*t_InventoryWrapper_Awake)(void* self, void* method);
typedef void (*t_InventoryWrapper_AddCurrency)(void* self, int amount, void* method);

// Landing Gear & Screen/Input pointers
typedef void (*t_AircraftMio_ToggleLandingGear)(void* self, void* method);
typedef void (*t_HelicopterMio_ToggleLandingGear)(void* self, void* method);
typedef void (*t_StageUI_OnLandingGearButtonClicked)(void* self, void* method);
typedef void (*t_Aircraft_setGear)(void* self, bool down, void* method);
typedef void (*t_Aircraft_SetGearForReplay)(void* self, bool down, void* method);
typedef void (*t_ToggleOrHoldButton_set_IsOn)(void* self, bool value, void* method);
typedef void (*t_ToggleOrHoldButton_OnPointerDown)(void* self, void* eventData, void* method);
typedef void (*t_ToggleOrHoldButton_OnPointerUp)(void* self, void* eventData, void* method);
typedef void (*t_ToggleOrHoldButton_Flash)(void* self, void* method);
typedef bool (*t_Behaviour_get_enabled)(void* self, void* method);
typedef void* (*t_Object_Instantiate_Parent)(void* original, void* parent, bool instantiateInWorldSpace, void* method);
typedef bool (*t_Input_GetMouseButtonDown)(int button, void* method);
typedef void (*t_Input_get_mousePosition_Injected)(Vector3* ret);
typedef int (*t_Screen_get_width)(void* method);
typedef int (*t_Screen_get_height)(void* method);

// IL2CPP Runtime API Pointers
typedef void* (*t_il2cpp_domain_get)();
typedef void** (*t_il2cpp_domain_get_assemblies)(void* domain, size_t* size);
typedef void* (*t_il2cpp_assembly_get_image)(void* assembly);
typedef const char* (*t_il2cpp_image_get_name)(void* image);
typedef void* (*t_il2cpp_class_from_name)(void* image, const char* namespaze, const char* name);
typedef void (*t_il2cpp_runtime_class_init)(void* klass);
typedef void* (*t_il2cpp_class_get_field_from_name)(void* klass, const char* name);
typedef void (*t_il2cpp_field_static_get_value)(void* field, void* value);

static t_il2cpp_domain_get il2cpp_domain_get_fn = nullptr;
static t_il2cpp_domain_get_assemblies il2cpp_domain_get_assemblies_fn = nullptr;
static t_il2cpp_assembly_get_image il2cpp_assembly_get_image_fn = nullptr;
static t_il2cpp_image_get_name il2cpp_image_get_name_fn = nullptr;
static t_il2cpp_class_from_name il2cpp_class_from_name_fn = nullptr;
static t_il2cpp_runtime_class_init il2cpp_runtime_class_init_fn = nullptr;
static t_il2cpp_class_get_field_from_name il2cpp_class_get_field_from_name_fn = nullptr;
static t_il2cpp_field_static_get_value il2cpp_field_static_get_value_fn = nullptr;

static t_StageUI_OnLandingGearButtonClicked StageUI_OnLandingGearButtonClicked = nullptr;
static t_ToggleOrHoldButton_Flash ToggleOrHoldButton_Flash = nullptr;

static t_Object_Instantiate_Parent Object_Instantiate_Parent = nullptr;
static t_Camera_WorldToScreenPoint Camera_WorldToScreenPoint = nullptr;
static t_Camera_get_main Camera_get_main = nullptr;
static t_Component_get_transform Component_get_transform = nullptr;
static t_GameObject_get_transform GameObject_get_transform = nullptr;
static t_Transform_get_position Transform_get_position = nullptr;
static t_Transform_set_position Transform_set_position = nullptr;
static t_Transform_get_forward Transform_get_forward = nullptr;
static t_IUnit_GetUnitType IUnit_GetUnitType = nullptr;
static t_PhysicsObject_get_Velocity orig_PhysicsObject_get_Velocity = nullptr;
static t_PhysicsObject_get_Velocity PhysicsObject_get_Velocity = nullptr;
Vector3 hook_PhysicsObject_get_Velocity(void* self);
static t_GeneralHUD_LateUpdate orig_GeneralHUD_LateUpdate = nullptr;
static t_Turret_FixedUpdate orig_Turret_FixedUpdate = nullptr;
static t_CameraShakeManager_GunfireAt orig_CameraShakeManager_GunfireAt = nullptr;
void hook_Turret_FixedUpdate(void* self);
void hook_CameraShakeManager_GunfireAt(Vector3 position, float magnitude);

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

static t_CameraManager_Awake orig_CameraManager_Awake = nullptr;
static t_CameraManager_SetCameraByType orig_CameraManager_SetCameraByType = nullptr;
static t_CameraManager_SetCamera orig_CameraManager_SetCamera = nullptr;
static t_VehicleSeat_InitializeHUD VehicleSeat_InitializeHUD = nullptr;
static void* g_CameraManagerInstance = nullptr;
static t_Airplane_FixedUpdate orig_Airplane_FixedUpdate = nullptr;
static t_AircraftMio_SetAirCraftControllerType AircraftMio_SetAirCraftControllerType = nullptr;
static t_Joystick_Start orig_Joystick_Start = nullptr;
static t_UltimateJoystick_DisableJoystick UltimateJoystick_DisableJoystick = nullptr;
static t_GameObject_SetActive GameObject_SetActive = nullptr;
static t_Component_get_gameObject Component_get_gameObject = nullptr;

static t_InventoryWrapper_Awake orig_InventoryWrapper_Awake = nullptr;
static t_InventoryWrapper_AddCurrency orig_InventoryWrapper_AddCurrency = nullptr;
static t_InventoryWrapper_AddCurrency InventoryWrapper_AddCurrency = nullptr;

static t_AircraftMio_ToggleLandingGear AircraftMio_ToggleLandingGear = nullptr;
static t_AircraftMio_ToggleLandingGear orig_AircraftMio_ToggleLandingGear = nullptr;
static t_HelicopterMio_ToggleLandingGear HelicopterMio_ToggleLandingGear = nullptr;
static t_StageUI_OnLandingGearButtonClicked orig_StageUI_OnLandingGearButtonClicked = nullptr;
static t_Aircraft_setGear Aircraft_setGear = nullptr;
static t_Aircraft_SetGearForReplay Aircraft_SetGearForReplay = nullptr;
static t_ToggleOrHoldButton_set_IsOn ToggleOrHoldButton_set_IsOn = nullptr;
static t_ToggleOrHoldButton_OnPointerDown orig_ToggleOrHoldButton_OnPointerDown = nullptr;
static t_ToggleOrHoldButton_OnPointerUp orig_ToggleOrHoldButton_OnPointerUp = nullptr;
static t_Behaviour_get_enabled Behaviour_get_enabled = nullptr;
static t_Input_GetMouseButtonDown Input_GetMouseButtonDown = nullptr;
static t_Input_get_mousePosition_Injected Input_get_mousePosition_Injected = nullptr;
static t_Screen_get_width Screen_get_width = nullptr;
static t_Screen_get_height Screen_get_height = nullptr;

static void* s_LastStageUI = nullptr;
static void* s_ClonedGearButtonComp = nullptr;
static uint64_t s_LastGearToggleMs = 0;

static std::atomic<int> g_ActivePlayerUnitType{0};
static std::atomic<bool> g_PendingGearToggle{false};
static std::atomic<std::time_t> g_GearButtonFlashTime{0};

// -----------------------------------------------------------------------------
// Global State & Buffers
// -----------------------------------------------------------------------------
static uintptr_t g_Il2CppBase = 0;
static void* g_SpawnManagerInstance = nullptr;
static void* g_JoystickInstance = nullptr;
static void* g_InventoryWrapperInstance = nullptr;

static Vector3 g_PlayerVehiclePos(0, 0, 0);
static std::atomic<bool> g_HasPlayerVehiclePos(false);

static std::atomic<bool> g_OpenMultiplayerRequested(false);
static uint64_t g_LastTriggerTime = 0;

static std::atomic<int> g_CurrentCameraType(0);
static std::atomic<int> g_PendingGiveAmount(0);

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

// -----------------------------------------------------------------------------
// Multi-Stage Combat Lead Stabilizer (v6.0 - Anti-Jitter Engine)
// -----------------------------------------------------------------------------
enum LeadStabilizerMode {
    LEAD_MODE_OFF = 0,               // Variant 0: Off (Raw network derivative)
    LEAD_MODE_EMA = 1,               // Variant 1: Low-Pass Exponential Moving Average (EMA)
    LEAD_MODE_ACCEL_CLAMP = 2,       // Variant 2: Kinematic Acceleration Clamping (Physical G-Limiter)
    LEAD_MODE_VISUAL_DAMPING = 3,    // Variant 3: Adaptive Spring-Damper Screen Smoothing
    LEAD_MODE_ULTIMATE = 4           // Variant 4: Ultimate Combined Hybrid (All 3 Tiers Active)
};

static std::atomic<int> g_LeadStabilizerMode{LEAD_MODE_ULTIMATE};
static std::atomic<float> g_LeadEmaFactor{12.0f};          // 1/s (typical 8.0 - 20.0)
static std::atomic<float> g_LeadMaxAccelG{9.0f};           // Max allowable acceleration in G (9.0G = 88.3 m/s^2)
static std::atomic<float> g_LeadVisualDampFactor{16.0f};   // Screen spring stiffness (10.0 - 25.0)
static std::atomic<bool> g_GlobalVelocityHookEnabled{false}; // Disabled by default: preserves 100% native rocket and game physics!
static std::atomic<bool> g_LeadEspEnabled{true};           // Draw custom ImGui lead HUD
static std::atomic<bool> g_LeadHudBadgeEnabled{true};      // Draw top status badge

struct ScreenLeadTarget {
    float leadX, leadY;
    float targetX, targetY;
    float distance;
    float flightTime;
    bool hasTargetLine;
    int targetType;
    float targetSpeedKmh;
    int stabilizerMode;
};

#define MAX_TARGETS 128
static ScreenLeadTarget g_TargetsBuffer[2][MAX_TARGETS];
static int g_TargetsCount[2] = {0, 0};
static std::atomic<int> g_ActiveBufferIdx(0);
static std::atomic<uint64_t> g_LastHudUpdateMs{0};

// Check if pointer looks like valid userspace memory on 64-bit Android (ARM64)
inline bool IsValidPtr(const void* ptr) {
    if (!ptr) return false;
    uintptr_t p = (uintptr_t)ptr & 0x00FFFFFFFFFFFFFFULL;
    // User-space pointers on 64-bit Android are >= 4 GB (0x100000000ULL).
    // This rejects 32-bit IL2CPP metadata tokens (e.g. 0x2001a5d9).
    return p >= 0x100000000ULL && p <= 0x00007FFFFFFFFFFFULL;
}

inline bool IsUnityObjectAlive(const void* uObj) {
    if (!IsValidPtr(uObj)) return false;
    void* nativePtr = *(void**)((uintptr_t)uObj + 0x10); // UnityEngine.Object.m_CachedPtr
    return IsValidPtr(nativePtr);
}

static inline void PatchDword(uintptr_t addr, uint32_t instruction) {
    MemoryPatch(addr, &instruction, sizeof(instruction)).Modify();
}

static void* s_StageUIField = nullptr;
static bool s_StageUIResolved = false;

static void* GetStageUIInstance() {
    if (!s_StageUIResolved) {
        if (!il2cpp_domain_get_fn || !il2cpp_domain_get_assemblies_fn ||
            !il2cpp_assembly_get_image_fn || !il2cpp_image_get_name_fn ||
            !il2cpp_class_from_name_fn || !il2cpp_class_get_field_from_name_fn) {
            return nullptr;
        }
        void* domain = il2cpp_domain_get_fn();
        if (!IsValidPtr(domain)) return nullptr;

        size_t count = 0;
        void** assemblies = il2cpp_domain_get_assemblies_fn(domain, &count);
        if (!assemblies || count == 0) return nullptr;

        void* stageUI_class = nullptr;
        for (size_t i = 0; i < count; i++) {
            if (!IsValidPtr(assemblies[i])) continue;
            void* image = il2cpp_assembly_get_image_fn(assemblies[i]);
            if (!IsValidPtr(image)) continue;
            const char* imgName = il2cpp_image_get_name_fn(image);
            if (imgName && strcmp(imgName, "Assembly-CSharp.dll") == 0) {
                stageUI_class = il2cpp_class_from_name_fn(image, "", "StageUI");
                break;
            }
        }
        if (IsValidPtr(stageUI_class)) {
            if (il2cpp_runtime_class_init_fn) {
                il2cpp_runtime_class_init_fn(stageUI_class);
            }
            s_StageUIField = il2cpp_class_get_field_from_name_fn(stageUI_class, "instance");
            if (s_StageUIField) {
                s_StageUIResolved = true;
                LOGI("[C-RAM-MOD] StageUI.instance FieldInfo resolved successfully: %p", s_StageUIField);
            }
        }
    }

    if (s_StageUIField && il2cpp_field_static_get_value_fn) {
        void* stageUI = nullptr;
        il2cpp_field_static_get_value_fn(s_StageUIField, &stageUI);
        if (IsUnityObjectAlive(stageUI)) {
            return stageUI;
        }
    }
    return nullptr;
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
// Multi-Stage Combat Lead Stabilizer Engine (v6.0 Anti-Jitter Filter Core)
// -----------------------------------------------------------------------------
struct TargetVelocityFilter {
    uintptr_t targetId = 0;
    Vector3 lastRawVelocity{0, 0, 0};
    Vector3 smoothedVelocity{0, 0, 0};
    uint64_t lastTimestampMs = 0;
    float lastScreenX = 0.0f;
    float lastScreenY = 0.0f;
    bool isScreenInit = false;
    bool isVelocityInit = false;
    uint32_t sampleCount = 0;
};

#define MAX_TRACKED_TARGETS 64
static TargetVelocityFilter g_TargetFilters[MAX_TRACKED_TARGETS];
static std::mutex g_FilterMutex;

static void ClearTargetFilters() {
    std::lock_guard<std::mutex> lock(g_FilterMutex);
    for (int i = 0; i < MAX_TRACKED_TARGETS; i++) {
        g_TargetFilters[i].targetId = 0;
        g_TargetFilters[i].isVelocityInit = false;
        g_TargetFilters[i].isScreenInit = false;
        g_TargetFilters[i].lastTimestampMs = 0;
        g_TargetFilters[i].sampleCount = 0;
    }
}

// Variant 1 & 2 & 4: Velocity Stabilization for Network Aircraft
static Vector3 FilterVelocityForTarget(void* target, const Vector3& rawVel, uint64_t nowMs) {
    if (!IsValidPtr(target)) return rawVel;
    int mode = g_LeadStabilizerMode.load();
    if (mode == LEAD_MODE_OFF) return rawVel;

    // Check if target has isNetworkGhost enabled (offset 0xA0 in IUnit)
    bool isGhost = false;
    if (IsValidPtr((void*)((uintptr_t)target + 0xA0))) {
        isGhost = *(bool*)((uintptr_t)target + 0xA0);
    }

    // In Mode 1, 2, 4 we filter all network ghosts, and in Ultimate mode we filter any target
    if (!isGhost && mode != LEAD_MODE_ULTIMATE) {
        return rawVel;
    }

    uintptr_t targetId = (uintptr_t)target;
    std::lock_guard<std::mutex> lock(g_FilterMutex);

    int bestSlot = -1;
    for (int i = 0; i < MAX_TRACKED_TARGETS; i++) {
        if (g_TargetFilters[i].targetId == targetId) {
            bestSlot = i;
            break;
        }
        if (g_TargetFilters[i].targetId == 0 && bestSlot == -1) {
            bestSlot = i;
        }
    }

    // LRU timeout eviction if table full (> 2000ms stale)
    if (bestSlot == -1) {
        for (int i = 0; i < MAX_TRACKED_TARGETS; i++) {
            if (nowMs - g_TargetFilters[i].lastTimestampMs > 2000) {
                bestSlot = i;
                g_TargetFilters[i].isVelocityInit = false;
                g_TargetFilters[i].isScreenInit = false;
                break;
            }
        }
        if (bestSlot == -1) return rawVel;
    }

    TargetVelocityFilter& state = g_TargetFilters[bestSlot];
    if (state.targetId != targetId || !state.isVelocityInit) {
        state.targetId = targetId;
        state.lastRawVelocity = rawVel;
        state.smoothedVelocity = rawVel;
        state.lastTimestampMs = nowMs;
        state.isVelocityInit = true;
        state.sampleCount = 1;
        return rawVel;
    }

    // Same frame caching (prevents over-filtering on multiple calls per frame)
    if (nowMs <= state.lastTimestampMs) {
        return state.smoothedVelocity;
    }

    float dt = (float)(nowMs - state.lastTimestampMs) / 1000.0f;
    state.lastTimestampMs = nowMs;
    state.sampleCount++;

    // Clamp dt to reasonable bounds (1ms to 350ms)
    if (dt > 0.35f) {
        // Discontinuity / Teleport: reset filter baseline
        state.smoothedVelocity = rawVel;
        state.lastRawVelocity = rawVel;
        return rawVel;
    }
    if (dt < 0.001f) dt = 0.001f;

    Vector3 workingVel = rawVel;

    // --- Variant 2: Kinematic Acceleration Clamping (Physical G-Limiter) ---
    if (mode == LEAD_MODE_ACCEL_CLAMP || mode == LEAD_MODE_ULTIMATE) {
        float maxG = g_LeadMaxAccelG.load();
        float maxA = maxG * 9.80665f; // e.g. 9.0G = 88.26 m/s^2
        float maxDeltaV = maxA * dt;

        Vector3 deltaV = workingVel - state.smoothedVelocity;
        float deltaVMag = Vector3::Magnitude(deltaV);

        if (deltaVMag > maxDeltaV && deltaVMag > 0.0001f) {
            float clampScale = maxDeltaV / deltaVMag;
            workingVel = state.smoothedVelocity + deltaV * clampScale;
        }
    }

    // --- Variant 1: Low-Pass Exponential Moving Average (EMA) ---
    if (mode == LEAD_MODE_EMA || mode == LEAD_MODE_ULTIMATE) {
        float lambda = g_LeadEmaFactor.load(); // e.g. 12.0 1/s
        float alpha = 1.0f - expf(-lambda * dt);
        if (alpha > 1.0f) alpha = 1.0f;
        if (alpha < 0.01f) alpha = 0.01f;

        state.smoothedVelocity = Vector3::Lerp(state.smoothedVelocity, workingVel, alpha);
    } else {
        state.smoothedVelocity = workingVel;
    }

    state.lastRawVelocity = rawVel;
    return state.smoothedVelocity;
}

// Screen-Space Lead Reticle Filtering:
// DISABLED/BYPASSED! Instantaneous world-to-screen projection guarantees zero camera lag/swinging when rotating turrets.
static void FilterScreenLeadPosition(void* target, float rawScreenX, float rawScreenY, float& outScreenX, float& outScreenY, uint64_t nowMs) {
    outScreenX = rawScreenX;
    outScreenY = rawScreenY;
}

// -----------------------------------------------------------------------------
// AC-130 Gunship Turret Recoil & Camera Shake Stabilization Hooks
// -----------------------------------------------------------------------------
// Neutralizes cumulative recoil impulse on the airframe when firing AC-130 cannons (20mm, 40mm, 105mm),
// preventing airframe rolling/capsizing, autopilot drift, and camera detachment/inversion.
void hook_Turret_FixedUpdate(void* self) {
    if (IsValidPtr(self)) {
        void* aws = *(void**)((uintptr_t)self + 0x30); // WeaponSystem.parent
        if (IsValidPtr(aws)) {
            void* parentUnit = *(void**)((uintptr_t)aws + 0x20); // AWS.parent (IUnit)
            if (IsValidPtr(parentUnit) && IUnit_GetUnitType) {
                int uType = IUnit_GetUnitType(parentUnit);
                if (uType == 3) { // Air unit (including AC-130 gunship)
                    // Zero out physical recoil impulse on aircraft airframe to prevent rolling/capsizing
                    *(float*)((uintptr_t)self + 0x148) = 0.0f; // _recoilImpulsePending = 0
                    *(float*)((uintptr_t)self + 0x14C) = 0.0f; // _recoilTimeRemaining = 0
                }
            }
        }
    }
    if (orig_Turret_FixedUpdate) {
        orig_Turret_FixedUpdate(self);
    }
}

// Suppresses violent gunfire impulse shaking in AC130_OPERATOR camera (type 22)
// to maintain a rock-solid gyro-stabilized FLIR targeting pod view and prevent camera clipping/breakage.
void hook_CameraShakeManager_GunfireAt(Vector3 position, float magnitude) {
    int curCam = g_CurrentCameraType.load();
    if (curCam == 22) { // GameCameraType.AC130_OPERATOR
        return;
    }
    if (orig_CameraShakeManager_GunfireAt) {
        orig_CameraShakeManager_GunfireAt(position, magnitude);
    }
}

// Stub for PhysicsObject.get_Velocity (keeps native physics 100% unhindered)
Vector3 hook_PhysicsObject_get_Velocity(void* self) {
    if (!orig_PhysicsObject_get_Velocity) {
        return Vector3(0, 0, 0);
    }
    return orig_PhysicsObject_get_Velocity(self);
}


// -----------------------------------------------------------------------------
// Economy Hooks (InventoryWrapper)
// -----------------------------------------------------------------------------
void hook_InventoryWrapper_Awake(void* self, void* method) {
    g_InventoryWrapperInstance = self;
    LOGI("[C-RAM-MOD] InventoryWrapper::Awake captured! Instance: %p", self);
    if (orig_InventoryWrapper_Awake) {
        orig_InventoryWrapper_Awake(self, method);
    }
}

void hook_InventoryWrapper_AddCurrency(void* self, int amount, void* method) {
    g_InventoryWrapperInstance = self;
    if (orig_InventoryWrapper_AddCurrency) {
        orig_InventoryWrapper_AddCurrency(self, amount, method);
    }
}

// -----------------------------------------------------------------------------
// Joystick Hook (Captures active on-screen joystick)
// -----------------------------------------------------------------------------
void hook_Joystick_Start(void* self, void* method) {
    g_JoystickInstance = self;
    LOGI("[C-RAM-MOD] Joystick::Start captured! Instance: %p", self);
    if (orig_Joystick_Start) {
        orig_Joystick_Start(self, method);
    }
}

// -----------------------------------------------------------------------------
// Cockpit Cursor Steering & UI Hiding via CameraManager
// -----------------------------------------------------------------------------
void ApplyCockpitCursorSteering(void* seat, int cameraType) {
    if (!IsValidPtr(seat)) return;

    void* parentSeatSys = *(void**)((uintptr_t)seat + 0x20); // VehicleSeat.parent
    if (!IsValidPtr(parentSeatSys)) return;

    void* unit = *(void**)((uintptr_t)parentSeatSys + 0x20); // SeatSystem.parent (IUnit)
    if (!IsValidPtr(unit)) return;

    // Helicopter & Non-airplane Safety: Check unit type
    int uType = 0;
    if (IUnit_GetUnitType) {
        uType = IUnit_GetUnitType(unit);
    }
    // Only apply arcade airplane steering if it is strictly an Air unit (3)
    if (uType != 3) {
        return;
    }

    if (cameraType == 9) { // UNIT_FPS_POV (Cockpit View)
        // 1. Resolve aircraft & airplane
        if (AircraftMio_SetAirCraftControllerType) {
            AircraftMio_SetAirCraftControllerType(unit, 3, nullptr);
            LOGI("[C-RAM-MOD] SetAirCraftControllerType(ArcadeCursor=3) applied in Cockpit POV!");
        }
        void* airplane = *(void**)((uintptr_t)unit + OFFSET_MIO_AIRPLANE); // AircraftMio.airplane
        if (IsValidPtr(airplane)) {
            *(bool*)((uintptr_t)airplane + OFFSET_AIRPLANE_CURSORSTEERING) = true;
            LOGI("[C-RAM-MOD] airplane->cursorSteering forced to true in Cockpit POV!");

            // Ensure aimReticle and cursor GameObjects are active
            if (Component_get_gameObject && GameObject_SetActive) {
                void* reticle = *(void**)((uintptr_t)airplane + OFFSET_AIRPLANE_AIMRETICLE);
                if (IsValidPtr(reticle)) {
                    void* reticleGO = Component_get_gameObject(reticle, nullptr);
                    if (IsValidPtr(reticleGO)) GameObject_SetActive(reticleGO, true, nullptr);
                }
                void* cursor = *(void**)((uintptr_t)airplane + OFFSET_AIRPLANE_CURSOR);
                if (IsValidPtr(cursor)) {
                    void* cursorGO = Component_get_gameObject(cursor, nullptr);
                    if (IsValidPtr(cursorGO)) GameObject_SetActive(cursorGO, true, nullptr);
                }
            }
        }

    }
}

void hook_CameraManager_Awake(void* self, void* method) {
    g_CameraManagerInstance = self;
    LOGI("[C-RAM-CAM] CameraManager::Awake captured! Instance: %p", self);
    if (orig_CameraManager_Awake) {
        orig_CameraManager_Awake(self, method);
    }
}

void hook_CameraManager_SetCameraByType(void* self, int type, void* method) {
    g_CameraManagerInstance = self;
    g_CurrentCameraType.store(type);
    void* seat = nullptr;
    if (IsValidPtr(self)) {
        seat = *(void**)((uintptr_t)self + 0x1A0); // CameraManager.selectedSeat
    }
    LOGI("[C-RAM-CAM] SetCameraByType: type=%d, selectedSeat=%p, camMgr=%p", type, seat, self);
    if (orig_CameraManager_SetCameraByType) {
        orig_CameraManager_SetCameraByType(self, type, method);
    }
    ApplyCockpitCursorSteering(seat, type);
}

void hook_CameraManager_SetCamera(void* self, int gameCameraType, void* follow, int unitType, void* method) {
    g_CameraManagerInstance = self;
    g_CurrentCameraType.store(gameCameraType);
    LOGI("[C-RAM-CAM] SetCamera: type=%d, followSeat=%p, unitType=%d, camMgr=%p", gameCameraType, follow, unitType, self);
    if (orig_CameraManager_SetCamera) {
        orig_CameraManager_SetCamera(self, gameCameraType, follow, unitType, method);
    }
    ApplyCockpitCursorSteering(follow, gameCameraType);
}

// -----------------------------------------------------------------------------
// Hook on Airplane.FixedUpdate (Keeps Cursor Steering Active in Cockpit POV)
// -----------------------------------------------------------------------------
void hook_Airplane_FixedUpdate(void* self, void* method) {
    if (IsValidPtr(self)) {
        void* parentMio = *(void**)((uintptr_t)self + OFFSET_AIRPLANE_PARENT);
        if (IsValidPtr(parentMio)) {
            int uType = IUnit_GetUnitType ? IUnit_GetUnitType(parentMio) : 0;
            if (uType == 3) { // Air only
                int curCam = g_CurrentCameraType.load();
                if (curCam == 0 && IsValidPtr(g_CameraManagerInstance)) {
                    curCam = *(int*)((uintptr_t)g_CameraManagerInstance + 0x1B8);
                }
                if (curCam == 9) { // In Cockpit POV
                    *(bool*)((uintptr_t)self + OFFSET_AIRPLANE_CURSORSTEERING) = true;
                    *(int*)((uintptr_t)parentMio + OFFSET_MIO_CONTROLLERTYPE) = 3; // ArcadeCursor
                }
            }
        }
    }
    if (orig_Airplane_FixedUpdate) {
        orig_Airplane_FixedUpdate(self, method);
    }
}


// -----------------------------------------------------------------------------
// Component Safety Guards (Eliminates NullReferenceExceptions on Ground Units)
// -----------------------------------------------------------------------------
void hook_TrackDeformer_Update(void* self) {
    if (!IsValidPtr(self)) return;
    void* initialVerts = *(void**)((uintptr_t)self + 0x40);
    void* currentVerts = *(void**)((uintptr_t)self + 0x58);
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
    if (s == "shahed launcher" || s == "shahed-launcher" || s == "shahedlauncher" || s == "launcher") return "ShahedLauncher";

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

    return input;
}

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

bool IsTurretOrStationary(const std::string& name) {
    if (name == "ShahedLauncher" ||
        name.find("Launcher") != std::string::npos ||
        name.find("launcher") != std::string::npos ||
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

    if (!prefab || !IsValidPtr(prefab)) {
        LOGE("[C-RAM-MOD] Prefab not found for '%s'!", canonical.c_str());
        return;
    }

    bool isAir = IsAirUnit(canonical);
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
                Vector3 basePos = cPos;
                if (g_HasPlayerVehiclePos.load()) {
                    basePos = g_PlayerVehiclePos;
                }

                spawnPos = basePos + groundFwd * spawnDist;
                if (req.alt >= 0.0f) {
                    spawnPos.Y = req.alt;
                }
                spawnRot = Quaternion::LookRotation(groundFwd * -1.0f);
            }
        }
    }

    bool spawned = false;

    // 1. Air Units (Su-57, B-2, Shahed, AC-130, FPV, etc.)
    if (isAir) {
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
    // 2. Ground Vehicles, Launchers & Turrets
    else {
        if (g_SpawnManagerInstance && SpawnManager_SpawnUnit) {
            try {
                bool isStationary = IsTurretOrStationary(canonical);
                bool needAI = req.setupAI && !isStationary;
                LOGI("[C-RAM-MOD] Spawning ground/launcher unit '%s' (needAI=%d, isStationary=%d)...",
                     canonical.c_str(), (int)needAI, (int)isStationary);
                void* unit = SpawnManager_SpawnUnit(g_SpawnManagerInstance, prefab, spawnPos, spawnRot, nullptr, needAI, nullptr);
                if (unit && IsValidPtr(unit)) {
                    spawned = true;
                    if (SpawnManager_SnapGroundUnitToSurface) {
                        try {
                            SpawnManager_SnapGroundUnitToSurface(g_SpawnManagerInstance, unit, nullptr);
                            LOGI("[C-RAM-MOD] Snapped '%s' to surface successfully!", canonical.c_str());
                        } catch (...) {
                            LOGE("[C-RAM-MOD] Exception in SnapGroundUnitToSurface");
                        }
                    }
                }
            } catch (...) {
                LOGE("[C-RAM-MOD] Exception inside SpawnManager ground call");
            }
        }
    }

    // Generic fallback ONLY if SpawnManager failed or was null
    if (!spawned && Object_Instantiate) {
        LOGI("[C-RAM-MOD] Spawning via Object.Instantiate fallback...");
        void* obj = Object_Instantiate(prefab, nullptr);
        if (obj && GameObject_get_transform && Transform_set_position) {
            void* tr = GameObject_get_transform(obj);
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
// Unified Arcade Flight Landing Gear Controller & Lighting Sync
// -----------------------------------------------------------------------------
void ToggleArcadeLandingGear() {
    uint64_t nowMs = getNowMs();
    if (nowMs - s_LastGearToggleMs < 300) {
        return; // debounce against duplicate hook invocations
    }
    s_LastGearToggleMs = nowMs;

    void* stageUI = GetStageUIInstance();
    void* playerUnit = nullptr;
    if (IsValidPtr(stageUI)) {
        playerUnit = *(void**)((uintptr_t)stageUI + 0x2A0); // StageUI.playerUnit
    }
    if (!IsValidPtr(playerUnit) && IsValidPtr(g_CameraManagerInstance)) {
        void* seat = *(void**)((uintptr_t)g_CameraManagerInstance + 0x1A0);
        if (IsValidPtr(seat)) {
            void* parentSeatSys = *(void**)((uintptr_t)seat + 0x20);
            if (IsValidPtr(parentSeatSys)) {
                playerUnit = *(void**)((uintptr_t)parentSeatSys + 0x20);
            }
        }
    }

    if (!IsValidPtr(playerUnit)) {
        LOGE("[C-RAM-MOD] ToggleArcadeLandingGear: playerUnit not found!");
        return;
    }

    int unitType = 0;
    if (IUnit_GetUnitType) {
        unitType = IUnit_GetUnitType(playerUnit);
    }
    if (unitType != 3) {
        LOGI("[C-RAM-MOD] ToggleArcadeLandingGear: unitType is %d, not airplane", unitType);
        return;
    }

    void* aircraftComp = *(void**)((uintptr_t)playerUnit + 0x1A8); // AircraftMio.aircraft
    if (!IsValidPtr(aircraftComp)) {
        LOGE("[C-RAM-MOD] ToggleArcadeLandingGear: aircraftComp is null!");
        return;
    }

    bool curDown = *(bool*)((uintptr_t)aircraftComp + 0x56); // Aircraft.isGearDown
    bool newDown = !curDown;

    // 1. Physically toggle Aircraft physics, animators, wheel colliders
    if (Aircraft_setGear) {
        Aircraft_setGear(aircraftComp, newDown, nullptr);
    }
    *(bool*)((uintptr_t)aircraftComp + 0x56) = newDown;

    // 2. Visually toggle 3D model parts in AircraftMio.LandingGearThings (0x1F8)
    void* gearThingsList = *(void**)((uintptr_t)playerUnit + 0x1F8);
    if (IsValidPtr(gearThingsList)) {
        void* itemsArray = *(void**)((uintptr_t)gearThingsList + 0x10);
        int count = *(int*)((uintptr_t)gearThingsList + 0x18);
        if (IsValidPtr(itemsArray) && count > 0 && count < 100) {
            for (int i = 0; i < count; i++) {
                void* gObj = *(void**)((uintptr_t)itemsArray + 0x20 + (i * sizeof(void*)));
                if (IsValidPtr(gObj) && GameObject_SetActive) {
                    GameObject_SetActive(gObj, newDown, nullptr);
                }
            }
        }
    }

    // 3. Persistent button lighting:
    // Set Mode = Toggle (0) and flashTimer = 0 so Update doesn't turn off _isOn!
    if (IsUnityObjectAlive(s_ClonedGearButtonComp)) {
        *(int*)((uintptr_t)s_ClonedGearButtonComp + 0x20) = 0; // Mode = Toggle
        *(float*)((uintptr_t)s_ClonedGearButtonComp + 0x94) = 0.0f; // flashTimer = 0
        if (ToggleOrHoldButton_set_IsOn) {
            ToggleOrHoldButton_set_IsOn(s_ClonedGearButtonComp, newDown, nullptr);
        } else {
            *(bool*)((uintptr_t)s_ClonedGearButtonComp + 0x90) = newDown;
        }
    }

    if (IsValidPtr(stageUI)) {
        void* origBtn = *(void**)((uintptr_t)stageUI + 0x1A8);
        if (IsUnityObjectAlive(origBtn)) {
            *(int*)((uintptr_t)origBtn + 0x20) = 0; // Mode = Toggle
            *(float*)((uintptr_t)origBtn + 0x94) = 0.0f; // flashTimer = 0
            if (ToggleOrHoldButton_set_IsOn) {
                ToggleOrHoldButton_set_IsOn(origBtn, newDown, nullptr);
            } else {
                *(bool*)((uintptr_t)origBtn + 0x90) = newDown;
            }
        }
    }

    LOGI("[C-RAM-MOD] ToggleArcadeLandingGear SUCCESS: newDown=%d (persistent lit=%d)", (int)newDown, (int)newDown);
}

void hook_ToggleOrHoldButton_OnPointerDown(void* self, void* eventData, void* method) {
    if (self != nullptr && (self == s_ClonedGearButtonComp || (IsValidPtr(s_LastStageUI) && self == *(void**)((uintptr_t)s_LastStageUI + 0x1A8)))) {
        LOGI("[C-RAM-MOD] LandingGear button OnPointerDown intercepted!");
        ToggleArcadeLandingGear();
        return;
    }
    if (orig_ToggleOrHoldButton_OnPointerDown) {
        orig_ToggleOrHoldButton_OnPointerDown(self, eventData, method);
    }
}

void hook_ToggleOrHoldButton_OnPointerUp(void* self, void* eventData, void* method) {
    if (self != nullptr && (self == s_ClonedGearButtonComp || (IsValidPtr(s_LastStageUI) && self == *(void**)((uintptr_t)s_LastStageUI + 0x1A8)))) {
        *(bool*)((uintptr_t)self + 0x91) = false; // _held = false
        return;
    }
    if (orig_ToggleOrHoldButton_OnPointerUp) {
        orig_ToggleOrHoldButton_OnPointerUp(self, eventData, method);
    }
}

void hook_StageUI_OnLandingGearButtonClicked(void* self, void* method) {
    LOGI("[C-RAM-MOD] StageUI.OnLandingGearButtonClicked intercepted!");
    ToggleArcadeLandingGear();
}

void hook_AircraftMio_ToggleLandingGear(void* self, void* method) {
    if (IsValidPtr(self)) {
        void* aircraftComp = *(void**)((uintptr_t)self + 0x1A8);
        if (IsValidPtr(aircraftComp) && Behaviour_get_enabled && Behaviour_get_enabled(aircraftComp, nullptr)) {
            LOGI("[C-RAM-MOD] AircraftMio.ToggleLandingGear arcade intercepted!");
            ToggleArcadeLandingGear();
            return;
        }
    }
    if (orig_AircraftMio_ToggleLandingGear) {
        orig_AircraftMio_ToggleLandingGear(self, method);
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
        if (MpBootstrap) MpBootstrap();
        if (MpEntry_OnClicked) MpEntry_OnClicked(self);
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

    // 3. Economy / Give Currency Trigger
    int giveAmt = g_PendingGiveAmount.exchange(0);
    if (giveAmt > 0) {
        if (g_InventoryWrapperInstance && InventoryWrapper_AddCurrency) {
            InventoryWrapper_AddCurrency(g_InventoryWrapperInstance, giveAmt, nullptr);
            LOGI("[C-RAM-MOD] Added %d currency via InventoryWrapper!", giveAmt);
        } else {
            LOGE("[C-RAM-MOD] InventoryWrapper instance or AddCurrency function not ready!");
        }
    }

    // 4. Safe Landing Gear Toggle Handling (Screen Touch & TCP Command)
    void* playerUnit = nullptr;
    int playerUnitType = 0;
    if (IsValidPtr(g_CameraManagerInstance)) {
        void* seat = *(void**)((uintptr_t)g_CameraManagerInstance + 0x1A0); // CameraManager.selectedSeat
        if (IsValidPtr(seat)) {
            void* parentSeatSys = *(void**)((uintptr_t)seat + 0x20); // VehicleSeat.parent
            if (IsValidPtr(parentSeatSys)) {
                playerUnit = *(void**)((uintptr_t)parentSeatSys + 0x20); // SeatSystem.parent (IUnit)
                if (IsValidPtr(playerUnit) && IUnit_GetUnitType) {
                    playerUnitType = IUnit_GetUnitType(playerUnit);
                }
            }
        }
    }

    g_ActivePlayerUnitType.store(playerUnitType);

    // 5. StageUI: Clone native LandingGearButton into mobileArcadeAircraft for Arcade (Simplified) Airplane Mode
    void* stageUI = GetStageUIInstance();

    // Reset when exiting match or entering a new match or if StageUI changed
    if (stageUI != s_LastStageUI || !IsUnityObjectAlive(stageUI)) {
        s_LastStageUI = stageUI;
        s_ClonedGearButtonComp = nullptr;
    }

    // Check if cloned button was destroyed
    if (s_ClonedGearButtonComp != nullptr && !IsUnityObjectAlive(s_ClonedGearButtonComp)) {
        s_ClonedGearButtonComp = nullptr;
    }

    // ONLY clone if we are in a match with an AIRPLANE (playerUnitType == 3)
    // Helicopters (type 6) and ground vehicles are strictly excluded!
    if (IsUnityObjectAlive(stageUI) && playerUnitType == 3) {
        void* origLdgBtn = *(void**)((uintptr_t)stageUI + 0x1A8);      // StageUI.LandingGearButton (ToggleOrHoldButton)
        void* arcadeAircraftGO = *(void**)((uintptr_t)stageUI + 0x68); // StageUI.mobileArcadeAircraft (GameObject)

        if (IsUnityObjectAlive(origLdgBtn) && IsUnityObjectAlive(arcadeAircraftGO) && s_ClonedGearButtonComp == nullptr) {
            if (GameObject_get_transform && Object_Instantiate_Parent) {
                void* arcadeTr = GameObject_get_transform(arcadeAircraftGO);
                if (IsValidPtr(arcadeTr)) {
                    // Instantiate exact clone of native LandingGearButton as child of mobileArcadeAircraft
                    // worldPositionStays = false preserves exact native RectTransform layout, sizing, and styling
                    void* clonedComp = Object_Instantiate_Parent(origLdgBtn, arcadeTr, false, nullptr);
                    if (IsUnityObjectAlive(clonedComp)) {
                        s_ClonedGearButtonComp = clonedComp;
                        *(int*)((uintptr_t)s_ClonedGearButtonComp + 0x20) = 0; // Mode = Toggle (0)
                        *(float*)((uintptr_t)s_ClonedGearButtonComp + 0x94) = 0.0f; // flashTimer = 0.0f
                        if (Component_get_gameObject && GameObject_SetActive) {
                            void* clonedGO = Component_get_gameObject(clonedComp, nullptr);
                            if (IsValidPtr(clonedGO)) {
                                GameObject_SetActive(clonedGO, true, nullptr);
                            }
                        }
                        // Synchronize initial lighting state with physical gear state
                        if (IsValidPtr(playerUnit)) {
                            void* aircraftComp = *(void**)((uintptr_t)playerUnit + 0x1A8);
                            if (IsValidPtr(aircraftComp)) {
                                bool isDown = *(bool*)((uintptr_t)aircraftComp + 0x56);
                                if (ToggleOrHoldButton_set_IsOn) {
                                    ToggleOrHoldButton_set_IsOn(s_ClonedGearButtonComp, isDown, nullptr);
                                }
                            }
                        }
                        LOGI("[C-RAM-MOD] Successfully cloned native LandingGearButton into mobileArcadeAircraft with Mode=Toggle!");
                    }
                }
            }
        }
    }

    // 6. Keep Landing Gear Button lighting permanently synced with aircraft physical gear state
    if (IsUnityObjectAlive(s_ClonedGearButtonComp) && IsValidPtr(playerUnit)) {
        void* aircraftComp = *(void**)((uintptr_t)playerUnit + 0x1A8);
        if (IsValidPtr(aircraftComp)) {
            bool isGearDown = *(bool*)((uintptr_t)aircraftComp + 0x56);
            bool btnIsOn = *(bool*)((uintptr_t)s_ClonedGearButtonComp + 0x90);
            if (btnIsOn != isGearDown) {
                *(int*)((uintptr_t)s_ClonedGearButtonComp + 0x20) = 0;
                *(float*)((uintptr_t)s_ClonedGearButtonComp + 0x94) = 0.0f;
                if (ToggleOrHoldButton_set_IsOn) {
                    ToggleOrHoldButton_set_IsOn(s_ClonedGearButtonComp, isGearDown, nullptr);
                } else {
                    *(bool*)((uintptr_t)s_ClonedGearButtonComp + 0x90) = isGearDown;
                }
            }
        }
    }

    // 7. Handle Landing Gear Toggle from TCP Command
    if (g_PendingGearToggle.exchange(false)) {
        ToggleArcadeLandingGear();
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
                while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) {
                    buf[--n] = '\0';
                }

                LOGI("[C-RAM-MOD] TCP Command received: '%s'", buf);

                char reply[512];

                uint64_t nowMs = getNowMs();
                static std::string s_LastCmdStr = "";
                static uint64_t s_LastCmdTime = 0;
                if (s_LastCmdStr == buf && (nowMs - s_LastCmdTime) < 500) {
                    LOGI("[C-RAM-MOD] Duplicate command '%s' ignored (within %llums)", buf, (unsigned long long)(nowMs - s_LastCmdTime));
                    snprintf(reply, sizeof(reply), "OK: Duplicate ignored\n");
                    write(client_fd, reply, strlen(reply));
                    close(client_fd);
                    continue;
                }
                s_LastCmdStr = buf;
                s_LastCmdTime = nowMs;

                if (strcasecmp(buf, "ping") == 0) {
                    snprintf(reply, sizeof(reply), "PONG: C-RAM Mod v6.0 Online\n");
                } else if (strcasecmp(buf, "help") == 0) {
                    snprintf(reply, sizeof(reply),
                             "Commands:\n"
                             "  ping\n"
                             "  give <amount>\n"
                             "  spawn <unit> [noai] [dist <meters>] [alt <meters>]\n"
                             "  lead [status|mode <0-4>|smooth <f>|clamp <G>|damp <f>|hook <0|1>|esp <0|1>]\n"
                             "  list\n"
                             "  mp\n"
                             "  gear\n");
                } else if (strcasecmp(buf, "list") == 0) {
                    snprintf(reply, sizeof(reply),
                             "Units Available:\n"
                             "  Ground: df-21d, tor, tunguska, pantsir, buk, patriot, shilka, abrams, t90, t72\n"
                             "  Air: su57, b2, shahed, ac130, su25, su27, mig29, f14, f15, f16, f18, f35, fpv\n"
                             "  Heli: ka52, mi28, mi24, apache, cobra\n"
                             "  Turrets: shahedlauncher, phalanx, kashtan, searam, goalkeeper\n");
                } else if (strncasecmp(buf, "give", 4) == 0) {
                    char* p = buf + 4;
                    while (*p == ' ') p++;
                    int amount = atoi(p);
                    if (amount <= 0) amount = 10000;
                    g_PendingGiveAmount.store(amount);
                    snprintf(reply, sizeof(reply), "OK: Added %d currency queued\n", amount);
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
                } else if (strcasecmp(buf, "gear") == 0 || strcasecmp(buf, "ldg") == 0) {
                    g_PendingGearToggle.store(true);
                    snprintf(reply, sizeof(reply), "OK: Landing gear toggle requested\n");
                } else if (strncasecmp(buf, "lead", 4) == 0) {
                    char* args = buf + 4;
                    while (*args == ' ') args++;
                    if (strncasecmp(args, "mode", 4) == 0) {
                        char* valStr = args + 4;
                        while (*valStr == ' ') valStr++;
                        int m = atoi(valStr);
                        if (m < 0) m = 0;
                        if (m > 4) m = 4;
                        g_LeadStabilizerMode.store(m);
                        const char* modeNames[] = {
                            "0: OFF (Raw Network Output)",
                            "1: EMA Low-Pass Filter",
                            "2: Kinematic Acceleration Clamping (G-Limiter)",
                            "3: Adaptive Visual Screen Damping",
                            "4: Ultimate Combined Hybrid (All 3 Tiers Active)"
                        };
                        snprintf(reply, sizeof(reply), "OK: Lead Stabilizer Mode set to [%s]\n", modeNames[m]);
                    } else if (strncasecmp(args, "smooth", 6) == 0) {
                        char* valStr = args + 6;
                        while (*valStr == ' ') valStr++;
                        float factor = (float)atof(valStr);
                        if (factor < 1.0f) factor = 1.0f;
                        if (factor > 50.0f) factor = 50.0f;
                        g_LeadEmaFactor.store(factor);
                        snprintf(reply, sizeof(reply), "OK: Lead EMA Smoothing Factor set to %.1f (1/s)\n", factor);
                    } else if (strncasecmp(args, "clamp", 5) == 0) {
                        char* valStr = args + 5;
                        while (*valStr == ' ') valStr++;
                        float g = (float)atof(valStr);
                        if (g < 1.0f) g = 1.0f;
                        if (g > 30.0f) g = 30.0f;
                        g_LeadMaxAccelG.store(g);
                        snprintf(reply, sizeof(reply), "OK: Lead Max Accel G-Clamp set to %.1f G (%.1f m/s^2)\n", g, g * 9.80665f);
                    } else if (strncasecmp(args, "damp", 4) == 0) {
                        char* valStr = args + 4;
                        while (*valStr == ' ') valStr++;
                        float d = (float)atof(valStr);
                        if (d < 1.0f) d = 1.0f;
                        if (d > 50.0f) d = 50.0f;
                        g_LeadVisualDampFactor.store(d);
                        snprintf(reply, sizeof(reply), "OK: Lead Visual Damping Factor set to %.1f\n", d);
                    } else if (strncasecmp(args, "hook", 4) == 0) {
                        char* valStr = args + 4;
                        while (*valStr == ' ') valStr++;
                        bool en = (atoi(valStr) != 0);
                        g_GlobalVelocityHookEnabled.store(en);
                        snprintf(reply, sizeof(reply), "OK: Global PhysicsObject.get_Velocity Hook is now %s\n", en ? "ENABLED" : "DISABLED");
                    } else if (strncasecmp(args, "esp", 3) == 0) {
                        char* valStr = args + 3;
                        while (*valStr == ' ') valStr++;
                        bool en = (atoi(valStr) != 0);
                        g_LeadEspEnabled.store(en);
                        snprintf(reply, sizeof(reply), "OK: Lead ESP Reticle is now %s\n", en ? "ENABLED" : "DISABLED");
                    } else if (strncasecmp(args, "badge", 5) == 0) {
                        char* valStr = args + 5;
                        while (*valStr == ' ') valStr++;
                        bool en = (atoi(valStr) != 0);
                        g_LeadHudBadgeEnabled.store(en);
                        snprintf(reply, sizeof(reply), "OK: Lead HUD Status Badge is now %s\n", en ? "ENABLED" : "DISABLED");
                    } else {
                        int curMode = g_LeadStabilizerMode.load();
                        const char* modeNames[] = {
                            "0: OFF (Raw)",
                            "1: EMA Low-Pass Filter",
                            "2: Kinematic Accel G-Clamp",
                            "3: Adaptive Visual Damping",
                            "4: Ultimate Combined Hybrid"
                        };
                        snprintf(reply, sizeof(reply),
                                 "Lead Stabilizer Status (v6.0):\n"
                                 "  Mode:           [%s]\n"
                                 "  EMA Factor:     %.1f /s\n"
                                 "  G-Clamp Limit:  %.1f G (%.1f m/s^2)\n"
                                 "  Visual Damping: %.1f\n"
                                 "  Global Hook:    %s\n"
                                 "  ImGui ESP:      %s\n"
                                 "  HUD Badge:      %s\n"
                                 "Commands:\n"
                                 "  lead mode <0-4>\n"
                                 "  lead smooth <factor>\n"
                                 "  lead clamp <G>\n"
                                 "  lead damp <factor>\n"
                                 "  lead hook <0|1>\n"
                                 "  lead esp <0|1>\n"
                                 "  lead badge <0|1>\n",
                                 modeNames[curMode],
                                 g_LeadEmaFactor.load(),
                                 g_LeadMaxAccelG.load(), g_LeadMaxAccelG.load() * 9.80665f,
                                 g_LeadVisualDampFactor.load(),
                                 g_GlobalVelocityHookEnabled.load() ? "ON" : "OFF",
                                 g_LeadEspEnabled.load() ? "ON" : "OFF",
                                 g_LeadHudBadgeEnabled.load() ? "ON" : "OFF");
                    }
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
// Lead Math Solver (Exact Quadratic Ballistic Solver with Relative Shooter Velocity)
// -----------------------------------------------------------------------------
static bool CalculateLead(
    const Vector3& targetPos,
    const Vector3& targetVel,
    const Vector3& gunPos,
    const Vector3& playerVel,
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

    // Relative target velocity in the shooter's moving frame of reference
    Vector3 relVel = targetVel - playerVel;

    float vSqr = Vector3::Dot(relVel, relVel);
    float bSqr = bulletSpeed * bulletSpeed;
    float a = vSqr - bSqr;
    float b = 2.0f * Vector3::Dot(rel, relVel);
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
    outLeadPos = targetPos + relVel * t;

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

    if (!IsUnityObjectAlive(self) || !Camera_WorldToScreenPoint || !Camera_get_main) {
        return;
    }

    uint64_t nowMs = getNowMs();
    g_LastHudUpdateMs.store(nowMs);

    // Ensure HUD has a valid, alive Camera assigned (vanilla UpdateIndicatorPositions needs this!)
    void* cam = *(void**)((uintptr_t)self + OFFSET_HUD_MAINCAMERA);
    if (!IsUnityObjectAlive(cam)) {
        cam = Camera_get_main();
        if (IsUnityObjectAlive(cam)) {
            *(void**)((uintptr_t)self + OFFSET_HUD_MAINCAMERA) = cam;
        }
    }
    if (!IsUnityObjectAlive(cam)) {
        return;
    }

    // Ensure HUD parentSeat is alive. If this seat belongs to a dead/destroyed plane, recover from CameraManager
    void* seat = *(void**)((uintptr_t)self + OFFSET_HUD_PARENTSEAT);
    if (!IsUnityObjectAlive(seat) && IsValidPtr(g_CameraManagerInstance)) {
        void* activeSeat = *(void**)((uintptr_t)g_CameraManagerInstance + 0x1A0); // CameraManager.selectedSeat
        if (IsUnityObjectAlive(activeSeat)) {
            *(void**)((uintptr_t)self + OFFSET_HUD_PARENTSEAT) = activeSeat;
            seat = activeSeat;
            LOGI("[C-RAM-HUD] Restored dead parentSeat -> activeSeat=%p", activeSeat);
        }
    }

    // If seat is still not alive, this HUD belongs to a dead plane wreckage; do not wipe active targets!
    if (!IsUnityObjectAlive(seat)) {
        return;
    }

    Vector3 gunPos(0, 0, 0);
    Vector3 playerVel(0, 0, 0);
    float bulletSpeed = 1050.0f;
    bool noGravity = false;
    bool foundTurret = false;

    if (IsValidPtr(seat)) {
        void* seatSys = *(void**)((uintptr_t)seat + 0x20); // VehicleSeat.parent (SeatSystem)
        if (IsValidPtr(seatSys)) {
            void* playerUnit = *(void**)((uintptr_t)seatSys + 0x20); // SeatSystem.parent (IUnit)
            if (IsValidPtr(playerUnit) && PhysicsObject_get_Velocity) {
                playerVel = PhysicsObject_get_Velocity(playerUnit);
            }
        }

        if (Component_get_transform && Transform_get_position) {
            void* seatTr = Component_get_transform(seat);
            if (IsValidPtr(seatTr)) {
                g_PlayerVehiclePos = Transform_get_position(seatTr);
                g_HasPlayerVehiclePos.store(true);
            }
        }

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
    int enemyCount = (IsValidPtr(enemyList) && IsValidPtr(enemyList->items)) ? enemyList->getSize() : 0;
    if (enemyCount <= 0) {
        // Fallback: check allDetectedUnits (0x130) if allEnemyUnits is empty
        enemyList = *(monoList<void*>**)((uintptr_t)self + 0x130);
        enemyCount = (IsValidPtr(enemyList) && IsValidPtr(enemyList->items)) ? enemyList->getSize() : 0;
    }

    static uint64_t lastHudLogMs = 0;
    if (nowMs - lastHudLogMs > 2000) {
        lastHudLogMs = nowMs;
        LOGI("[C-RAM-HUD] HUD=%p, seat=%p, cam=%p, rawEnemies=%d",
             self, seat, cam, enemyCount);
    }

    if (!IsValidPtr(enemyList) || !IsValidPtr(enemyList->items) || enemyCount <= 0 || enemyCount > 256) {
        return;
    }

    int writeIdx = 1 - g_ActiveBufferIdx.load();
    int count = 0;
    float screenHeight = (glHeight > 0) ? (float)glHeight : (float)ImGui::GetIO().DisplaySize.y;
    if (screenHeight <= 0.0f) screenHeight = 1080.0f;

    for (int i = 0; i < enemyCount && count < MAX_TARGETS; i++) {
        void* target = ((void**)enemyList->items->vector)[i];
        if (!IsValidPtr(target)) continue;

        bool isAlive = *(bool*)((uintptr_t)target + OFFSET_UNIT_ISALIVE);
        if (!isAlive) continue;

        int targetType = 3;
        if (IUnit_GetUnitType) {
            targetType = IUnit_GetUnitType(target);
            if (targetType != 3 && targetType != 6 && targetType != 7 && targetType != 5) {
                continue;
            }
        }

        void* tr = Component_get_transform ? Component_get_transform(target) : nullptr;
        if (!IsValidPtr(tr) || !Transform_get_position) continue;
        Vector3 targetPos = Transform_get_position(tr);

        Vector3 targetVel(0, 0, 0);
        uint64_t nowMs = getNowMs();
        if (PhysicsObject_get_Velocity) {
            Vector3 rawVel = PhysicsObject_get_Velocity(target);
            targetVel = FilterVelocityForTarget(target, rawVel, nowMs);
        }

        Vector3 leadPos(0, 0, 0);
        float flightTime = 0.0f;
        float distance = 0.0f;

        if (!CalculateLead(targetPos, targetVel, gunPos, playerVel, bulletSpeed, noGravity, leadPos, flightTime, distance)) {
            continue;
        }

        Vector3 leadScreen = Camera_WorldToScreenPoint(cam, leadPos);
        if (leadScreen.Z <= 0.0f) continue;

        Vector3 targetScreen = Camera_WorldToScreenPoint(cam, targetPos);

        float rawLeadScreenX = leadScreen.X;
        float rawLeadScreenY = screenHeight - leadScreen.Y;

        // Instantaneous world-to-screen projection: zero camera lag/swinging when panning
        float smoothLeadScreenX = rawLeadScreenX;
        float smoothLeadScreenY = rawLeadScreenY;

        ScreenLeadTarget& entry = g_TargetsBuffer[writeIdx][count];
        entry.leadX = smoothLeadScreenX;
        entry.leadY = smoothLeadScreenY;
        entry.distance = distance;
        entry.flightTime = flightTime;
        entry.targetType = targetType;
        entry.targetSpeedKmh = Vector3::Magnitude(targetVel) * 3.6f;
        entry.stabilizerMode = g_LeadStabilizerMode.load();

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

    static int lastLoggedCount = -1;
    if (count != lastLoggedCount) {
        lastLoggedCount = count;
        LOGI("[C-RAM-HUD] Lead targets updated: count=%d (activeBuffer=%d)", count, writeIdx);
    }
}


// -----------------------------------------------------------------------------
// ESP Drawing Loop (Render Thread inside eglSwapBuffers)
// -----------------------------------------------------------------------------
void DrawMenu() {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw) return;

    if (!g_LeadEspEnabled.load()) return;

    uint64_t lastUpdate = g_LastHudUpdateMs.load();
    if (lastUpdate == 0 || (getNowMs() - lastUpdate) > 200) {
        return;
    }

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
    LOGI("C-RAM Mod Thread Started (v5.0 - Cockpit Steering + Lead + Give + Spawner)");

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

    // Battle & Core pointers
    Camera_get_main = (t_Camera_get_main)(g_Il2CppBase + RVA_CAMERA_MAIN);
    Camera_WorldToScreenPoint = (t_Camera_WorldToScreenPoint)(g_Il2CppBase + RVA_CAMERA_W2S);
    Component_get_transform = (t_Component_get_transform)(g_Il2CppBase + RVA_COMP_TRANSFORM);
    GameObject_get_transform = (t_GameObject_get_transform)(g_Il2CppBase + RVA_GAMEOBJECT_TRANSFORM);
    Transform_get_position = (t_Transform_get_position)(g_Il2CppBase + RVA_TRANS_POS);
    Transform_set_position = (t_Transform_set_position)(g_Il2CppBase + RVA_TRANS_SETPOS);
    Transform_get_forward = (t_Transform_get_forward)(g_Il2CppBase + RVA_TRANS_GETFORWARD);
    IUnit_GetUnitType = (t_IUnit_GetUnitType)(g_Il2CppBase + RVA_UNIT_TYPE);
    PhysicsObject_get_Velocity = (t_PhysicsObject_get_Velocity)(g_Il2CppBase + RVA_PHYS_VEL);

    // Multiplayer pointers
    MpBootstrap = (t_MpBootstrap)(g_Il2CppBase + RVA_MP_BOOTSTRAP);
    MpEntry_OnClicked = (t_MpEntry_OnClicked)(g_Il2CppBase + RVA_MP_ENTRY_CLICKED);

    // Spawning & IL2CPP runtime pointers
    void* hIl2Cpp = dlopen("libil2cpp.so", RTLD_NOLOAD);
    if (!hIl2Cpp) hIl2Cpp = RTLD_DEFAULT;

    il2cpp_string_new_fn = (t_il2cpp_string_new)dlsym(hIl2Cpp, "il2cpp_string_new");

    il2cpp_domain_get_fn = (t_il2cpp_domain_get)dlsym(hIl2Cpp, "il2cpp_domain_get");
    if (!il2cpp_domain_get_fn) il2cpp_domain_get_fn = (t_il2cpp_domain_get)(g_Il2CppBase + 0x36ad8d4);

    il2cpp_domain_get_assemblies_fn = (t_il2cpp_domain_get_assemblies)dlsym(hIl2Cpp, "il2cpp_domain_get_assemblies");
    if (!il2cpp_domain_get_assemblies_fn) il2cpp_domain_get_assemblies_fn = (t_il2cpp_domain_get_assemblies)(g_Il2CppBase + 0x36ad8e0);

    il2cpp_assembly_get_image_fn = (t_il2cpp_assembly_get_image)dlsym(hIl2Cpp, "il2cpp_assembly_get_image");
    if (!il2cpp_assembly_get_image_fn) il2cpp_assembly_get_image_fn = (t_il2cpp_assembly_get_image)(g_Il2CppBase + 0x36ad3d8);

    il2cpp_image_get_name_fn = (t_il2cpp_image_get_name)dlsym(hIl2Cpp, "il2cpp_image_get_name");
    if (!il2cpp_image_get_name_fn) il2cpp_image_get_name_fn = (t_il2cpp_image_get_name)(g_Il2CppBase + 0x36adfac);

    il2cpp_class_from_name_fn = (t_il2cpp_class_from_name)dlsym(hIl2Cpp, "il2cpp_class_from_name");
    if (!il2cpp_class_from_name_fn) il2cpp_class_from_name_fn = (t_il2cpp_class_from_name)(g_Il2CppBase + 0x36ad410);

    il2cpp_runtime_class_init_fn = (t_il2cpp_runtime_class_init)dlsym(hIl2Cpp, "il2cpp_runtime_class_init");
    if (!il2cpp_runtime_class_init_fn) il2cpp_runtime_class_init_fn = (t_il2cpp_runtime_class_init)(g_Il2CppBase + 0x36adcfc);

    il2cpp_class_get_field_from_name_fn = (t_il2cpp_class_get_field_from_name)dlsym(hIl2Cpp, "il2cpp_class_get_field_from_name");
    if (!il2cpp_class_get_field_from_name_fn) il2cpp_class_get_field_from_name_fn = (t_il2cpp_class_get_field_from_name)(g_Il2CppBase + 0x36ad430);

    il2cpp_field_static_get_value_fn = (t_il2cpp_field_static_get_value)dlsym(hIl2Cpp, "il2cpp_field_static_get_value");
    if (!il2cpp_field_static_get_value_fn) il2cpp_field_static_get_value_fn = (t_il2cpp_field_static_get_value)(g_Il2CppBase + 0x36adac8);
    GameAssetCatalog_GetUnitPrefab = (t_GameAssetCatalog_GetUnitPrefab)(g_Il2CppBase + RVA_ASSET_CATALOG_GET_PREFAB);
    SpawnManager_SpawnUnit = (t_SpawnManager_SpawnUnit)(g_Il2CppBase + RVA_SPAWN_MANAGER_SPAWN_UNIT);
    SpawnManager_SpawnAirUnit = (t_SpawnManager_SpawnAirUnit)(g_Il2CppBase + RVA_SPAWN_MANAGER_SPAWN_AIR);
    SpawnManager_SpawnGroundUnit = (t_SpawnManager_SpawnGroundUnit)(g_Il2CppBase + RVA_SPAWN_MANAGER_SPAWN_GROUND);
    SpawnManager_SnapGroundUnitToSurface = (t_SpawnManager_SnapGroundUnitToSurface)(g_Il2CppBase + RVA_SPAWN_MANAGER_SNAP_GROUND);
    Object_Instantiate = (t_Object_Instantiate)(g_Il2CppBase + RVA_OBJECT_INSTANTIATE);

    // Aircraft & Cockpit pointers
    AircraftMio_SetAirCraftControllerType = (t_AircraftMio_SetAirCraftControllerType)(g_Il2CppBase + RVA_AIRCRAFTMIO_SETCONTROLLER);
    UltimateJoystick_DisableJoystick = (t_UltimateJoystick_DisableJoystick)(g_Il2CppBase + RVA_ULTIMATEJOYSTICK_DISABLE);
    GameObject_SetActive = (t_GameObject_SetActive)(g_Il2CppBase + RVA_GAMEOBJECT_SETACTIVE);
    Component_get_gameObject = (t_Component_get_gameObject)(g_Il2CppBase + RVA_COMP_GET_GAMEOBJECT);
    VehicleSeat_InitializeHUD = (t_VehicleSeat_InitializeHUD)(g_Il2CppBase + RVA_VEHICLESEAT_INITIALIZEHUD);

    // Economy pointers
    InventoryWrapper_AddCurrency = (t_InventoryWrapper_AddCurrency)(g_Il2CppBase + RVA_INVENTORY_ADDCURRENCY);

    // 1. Hook GeneralHUD.LateUpdate for Battle Lead Indicator
    void* targetHUDMethod = (void*)(g_Il2CppBase + RVA_HUD_LATEUPDATE);
    DobbyHook(targetHUDMethod, (void*)hook_GeneralHUD_LateUpdate, (void**)&orig_GeneralHUD_LateUpdate);

    // 1b. AC-130 Recoil Suppression & Camera Shake Protection
    void* targetTurretFixedUpdate = (void*)(g_Il2CppBase + RVA_TURRET_FIXEDUPDATE);
    DobbyHook(targetTurretFixedUpdate, (void*)hook_Turret_FixedUpdate, (void**)&orig_Turret_FixedUpdate);

    void* targetCameraShakeGunfire = (void*)(g_Il2CppBase + RVA_CAMERA_SHAKE_GUNFIRE);
    DobbyHook(targetCameraShakeGunfire, (void*)hook_CameraShakeManager_GunfireAt, (void**)&orig_CameraShakeManager_GunfireAt);

    // 2. Hook EventSystem.Update for safe execution on Unity Main Thread
    void* targetEventSystemMethod = (void*)(g_Il2CppBase + RVA_EVENTSYSTEM_UPDATE);
    DobbyHook(targetEventSystemMethod, (void*)hook_EventSystem_Update, (void**)&orig_EventSystem_Update);

    // 3. Hook CameraManager for Cockpit Cursor Steering & Control Hiding
    void* targetCameraAwake = (void*)(g_Il2CppBase + RVA_CAMERAMANAGER_AWAKE);
    DobbyHook(targetCameraAwake, (void*)hook_CameraManager_Awake, (void**)&orig_CameraManager_Awake);

    void* targetCameraByType = (void*)(g_Il2CppBase + RVA_CAMERAMANAGER_SETCAMERABYTYPE);
    DobbyHook(targetCameraByType, (void*)hook_CameraManager_SetCameraByType, (void**)&orig_CameraManager_SetCameraByType);

    void* targetCameraSet = (void*)(g_Il2CppBase + RVA_CAMERAMANAGER_SETCAMERA);
    DobbyHook(targetCameraSet, (void*)hook_CameraManager_SetCamera, (void**)&orig_CameraManager_SetCamera);

    // 4. Hook Airplane.FixedUpdate to guarantee 25G cursor physics in Cockpit POV
    void* targetAirplaneFixedUpdate = (void*)(g_Il2CppBase + RVA_AIRPLANE_FIXEDUPDATE);
    DobbyHook(targetAirplaneFixedUpdate, (void*)hook_Airplane_FixedUpdate, (void**)&orig_Airplane_FixedUpdate);

    // 5. Hook Joystick.Start to capture screen joystick
    void* targetJoystickStart = (void*)(g_Il2CppBase + RVA_JOYSTICK_START);
    DobbyHook(targetJoystickStart, (void*)hook_Joystick_Start, (void**)&orig_Joystick_Start);

    // 6. Hook InventoryWrapper for Economy Give Command
    void* targetInvAwake = (void*)(g_Il2CppBase + RVA_INVENTORY_AWAKE);
    DobbyHook(targetInvAwake, (void*)hook_InventoryWrapper_Awake, (void**)&orig_InventoryWrapper_Awake);

    void* targetInvAdd = (void*)(g_Il2CppBase + RVA_INVENTORY_ADDCURRENCY);
    DobbyHook(targetInvAdd, (void*)hook_InventoryWrapper_AddCurrency, (void**)&orig_InventoryWrapper_AddCurrency);

    // 7. Hook SpawnManager.Awake to capture active battle spawner
    void* targetSpawnManagerAwake = (void*)(g_Il2CppBase + RVA_SPAWN_MANAGER_AWAKE);
    DobbyHook(targetSpawnManagerAwake, (void*)hook_SpawnManager_Awake, (void**)&orig_SpawnManager_Awake);

    // 8. Hook IAPShopButton.OnEnable & IconsPanel.ShowStore (Diamond icon in hangar header)
    void* targetIAPShop = (void*)(g_Il2CppBase + RVA_IAP_SHOP_ONENABLE);
    DobbyHook(targetIAPShop, (void*)hook_IAPShopButton_OnEnable, (void**)&orig_IAPShopButton_OnEnable);

    void* targetShowStore = (void*)(g_Il2CppBase + RVA_ICONS_SHOWSTORE);
    DobbyHook(targetShowStore, (void*)hook_IconsPanel_ShowStore, (void**)&orig_IconsPanel_ShowStore);

    // 9. Component Guards: Eliminate NullReferenceExceptions on Ground Units & DF-21D
    void* targetTrackDeformer = (void*)(g_Il2CppBase + RVA_TRACK_DEFORMER_UPDATE);
    DobbyHook(targetTrackDeformer, (void*)hook_TrackDeformer_Update, (void**)&orig_TrackDeformer_Update);

    void* targetRotatingSensor = (void*)(g_Il2CppBase + RVA_ROTATING_SENSOR_UPDATE);
    DobbyHook(targetRotatingSensor, (void*)hook_RotatingSensor_Update, (void**)&orig_RotatingSensor_Update);

    // 10. Landing Gear & Screen/Input Pointers & Hooks
    AircraftMio_ToggleLandingGear = (t_AircraftMio_ToggleLandingGear)(g_Il2CppBase + RVA_AIRCRAFTMIO_TOGGLEGEAR);
    HelicopterMio_ToggleLandingGear = (t_HelicopterMio_ToggleLandingGear)(g_Il2CppBase + RVA_HELICOPTERMIO_TOGGLEGEAR);
    StageUI_OnLandingGearButtonClicked = (t_StageUI_OnLandingGearButtonClicked)(g_Il2CppBase + RVA_STAGEUI_ONLANDINGGEAR);
    Aircraft_setGear = (t_Aircraft_setGear)(g_Il2CppBase + RVA_AIRCRAFT_SETGEAR);
    Aircraft_SetGearForReplay = (t_Aircraft_SetGearForReplay)(g_Il2CppBase + RVA_AIRCRAFT_SETGEARFORREPLAY);
    ToggleOrHoldButton_set_IsOn = (t_ToggleOrHoldButton_set_IsOn)(g_Il2CppBase + RVA_BUTTON_SET_IS_ON);
    ToggleOrHoldButton_Flash = (t_ToggleOrHoldButton_Flash)(g_Il2CppBase + RVA_BUTTON_FLASH);
    Behaviour_get_enabled = (t_Behaviour_get_enabled)(g_Il2CppBase + RVA_BEHAVIOUR_GET_ENABLED);
    Object_Instantiate_Parent = (t_Object_Instantiate_Parent)(g_Il2CppBase + RVA_OBJECT_INSTANTIATE_PARENT);
    Input_GetMouseButtonDown = (t_Input_GetMouseButtonDown)(g_Il2CppBase + RVA_INPUT_GETMOUSEBUTTONDOWN);
    Input_get_mousePosition_Injected = (t_Input_get_mousePosition_Injected)(g_Il2CppBase + RVA_INPUT_GETMOUSEPOS_INJ);
    Screen_get_width = (t_Screen_get_width)(g_Il2CppBase + RVA_SCREEN_GETWIDTH);
    Screen_get_height = (t_Screen_get_height)(g_Il2CppBase + RVA_SCREEN_GETHEIGHT);

    // Install Landing Gear & Button Hooks
    DobbyHook((void*)(g_Il2CppBase + RVA_BUTTON_ONPOINTERDOWN), (void*)hook_ToggleOrHoldButton_OnPointerDown, (void**)&orig_ToggleOrHoldButton_OnPointerDown);
    DobbyHook((void*)(g_Il2CppBase + RVA_BUTTON_ONPOINTERUP), (void*)hook_ToggleOrHoldButton_OnPointerUp, (void**)&orig_ToggleOrHoldButton_OnPointerUp);
    DobbyHook((void*)(g_Il2CppBase + RVA_STAGEUI_ONLANDINGGEAR), (void*)hook_StageUI_OnLandingGearButtonClicked, (void**)&orig_StageUI_OnLandingGearButtonClicked);
    DobbyHook((void*)(g_Il2CppBase + RVA_AIRCRAFTMIO_TOGGLEGEAR), (void*)hook_AircraftMio_ToggleLandingGear, (void**)&orig_AircraftMio_ToggleLandingGear);

    // 10b. Livery Editor Decal Limit Removal (Unlimited Decals: 50 -> 9999)
    // 1. VehicleDecalPaintMode$$StartPlacement: NOP limit check (0x38C4808)
    PatchDword(g_Il2CppBase + 0x38C4808, 0x1F2003D5);

    // 2. VehicleDecalPaintMode$$DuplicateSelected: NOP limit check (0x38C53B4)
    PatchDword(g_Il2CppBase + 0x38C53B4, 0x1F2003D5);

    // 3. VehicleDecalPaintMode$$FitToEditorLimits: Branch over RemoveRange truncation (0x38C5DD0)
    PatchDword(g_Il2CppBase + 0x38C5DD0, 0x14000009);

    // 4. VehicleDecalPaintPanel$$RefreshHeader: movz w8, #9999 (Show '/ 9999') (0x38CFA6C)
    PatchDword(g_Il2CppBase + 0x38CFA6C, 0x5284E1E8);

    // 5. VehicleDecalPaintPanel$$RefreshHeader: Branch over red warning threshold (0x38CFAC4)
    PatchDword(g_Il2CppBase + 0x38CFAC4, 0x1400000D);

    LOGI("[C-RAM-MOD] Livery Decal Limit Patches applied successfully (Limit raised to 9999)");

    // 11. Start ADB Socket Listener Thread on 0.0.0.0:8888
    pthread_t sock_t;
    pthread_create(&sock_t, nullptr, socket_server_thread, nullptr);

    LOGI("C-RAM v6.6 Hooks Installed: Pure v5.5 HUD Lifecycle & Respawn Safety + Anti-Jitter Filters + AC-130 & Rocket Fixes Ready!");
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
