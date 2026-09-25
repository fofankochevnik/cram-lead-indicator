//
// C-RAM Air Target Lead Indicator & Multiplayer Menu Mod
//

#include <pthread.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <atomic>
#include <link.h>
#include <dlfcn.h>

#include "../Include/KittyMemory/MemoryPatch.h"
#include "../Include/ImGui.h"
#include "../Include/Drawing.h"
#include "../Include/Unity.h"
#include "../Include/Logger.h"

// -----------------------------------------------------------------------------
// RVAs from script.json & dump.cs (Relative to libil2cpp.so base)
// -----------------------------------------------------------------------------
#define RVA_CAMERA_MAIN        0x82D7D50
#define RVA_CAMERA_W2S         0x82D72A4
#define RVA_COMP_TRANSFORM     0x834A0F4
#define RVA_TRANS_POS          0x83601AC // UnityEngine.Transform.get_position
#define RVA_UNIT_TYPE          0x3F54748
#define RVA_PHYS_VEL           0x3EACAA4
#define RVA_HUD_LATEUPDATE     0x3F16414

// Multiplayer & Input RVAs
#define RVA_EVENTSYSTEM_UPDATE 0x86BE318 // UnityEngine.EventSystems.EventSystem.Update
#define RVA_INPUT_GET_MOUSE_POS 0x83D97C4 // UnityEngine.Input.get_mousePosition
#define RVA_INPUT_GET_MOUSE_BTN 0x83D9164 // UnityEngine.Input.GetMouseButton
#define RVA_MP_ENTRY_CLICKED   0x3E2A764 // MultiplayerEntryButton.OnClicked

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
typedef int (*t_IUnit_GetUnitType)(void* unit);
typedef Vector3 (*t_PhysicsObject_get_Velocity)(void* physicsObject);
typedef void (*t_GeneralHUD_LateUpdate)(void* self);

typedef void (*t_EventSystem_Update)(void* self);
typedef Vector3 (*t_Input_get_mousePosition)();
typedef bool (*t_Input_GetMouseButton)(int button);
typedef void (*t_MpEntry_OnClicked)(void* self);

static t_Camera_WorldToScreenPoint Camera_WorldToScreenPoint = nullptr;
static t_Camera_get_main Camera_get_main = nullptr;
static t_Component_get_transform Component_get_transform = nullptr;
static t_Transform_get_position Transform_get_position = nullptr;
static t_IUnit_GetUnitType IUnit_GetUnitType = nullptr;
static t_PhysicsObject_get_Velocity PhysicsObject_get_Velocity = nullptr;
static t_GeneralHUD_LateUpdate orig_GeneralHUD_LateUpdate = nullptr;

static t_EventSystem_Update orig_EventSystem_Update = nullptr;
static t_Input_get_mousePosition Input_get_mousePosition = nullptr;
static t_Input_GetMouseButton Input_GetMouseButton = nullptr;
static t_MpEntry_OnClicked MpEntry_OnClicked = nullptr;

// -----------------------------------------------------------------------------
// Global State & Thread-Safe Buffers
// -----------------------------------------------------------------------------
static uintptr_t g_Il2CppBase = 0;
static bool g_HUDActive = false;

// Touch & Multiplayer State
static std::atomic<float> g_TouchX(0.0f);
static std::atomic<float> g_TouchY(0.0f);
static std::atomic<bool> g_TouchDown(false);
static std::atomic<bool> g_OpenMultiplayerRequested(false);
static bool g_MultiplayerOpened = false;

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

// Check if pointer looks like valid userspace memory on 64-bit Android (Scudo / PAC)
inline bool IsValidPtr(const void* ptr) {
    if (!ptr) return false;
    uintptr_t p = (uintptr_t)ptr & 0x00FFFFFFFFFFFFFFULL;
    return p >= 0x100000ULL && p <= 0x00007FFFFFFFFFFFULL;
}

// -----------------------------------------------------------------------------
// Touch & Multiplayer Processing (Runs on Unity Main Thread)
// -----------------------------------------------------------------------------
static void ProcessTouchAndMultiplayer() {
    if (Input_get_mousePosition && Input_GetMouseButton) {
        Vector3 mPos = Input_get_mousePosition();
        bool mDown = Input_GetMouseButton(0);
        g_TouchX.store(mPos.X);
        float sH = (glHeight > 0) ? (float)glHeight : 1080.0f;
        g_TouchY.store(sH - mPos.Y); // Invert Y for ImGui coordinate system
        g_TouchDown.store(mDown);
    }

    if (g_OpenMultiplayerRequested.load()) {
        g_OpenMultiplayerRequested.store(false);
        LOGI("Triggering MultiplayerEntryButton::OnClicked on Unity Main Thread!");
        if (MpEntry_OnClicked) {
            MpEntry_OnClicked(nullptr);
            g_MultiplayerOpened = true;
            LOGI("MultiplayerEntryButton::OnClicked executed successfully!");
        } else {
            LOGE("MpEntry_OnClicked function pointer is null!");
        }
    }
}

// -----------------------------------------------------------------------------
// EventSystem.Update Hook (Active in ALL scenes: Menu, Hangar, Battle)
// -----------------------------------------------------------------------------
void hook_EventSystem_Update(void* self) {
    if (orig_EventSystem_Update) {
        orig_EventSystem_Update(self);
    }
    ProcessTouchAndMultiplayer();
}

// -----------------------------------------------------------------------------
// Lead Math Solver
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

    // Fallback if target faster than bullet or discriminant negative
    if (t <= 0.0f) {
        t = outDistance / bulletSpeed;
    }

    outFlightTime = t;
    outLeadPos = targetPos + targetVel * t;

    // Ballistic drop compensation (game Gravity = -9.0)
    if (!noGravity) {
        outLeadPos.Y += 0.5f * 9.0f * t * t;
    }

    return true;
}

// -----------------------------------------------------------------------------
// GeneralHUD.LateUpdate Hook (Runs on Unity Main Thread during Battle)
// -----------------------------------------------------------------------------
void hook_GeneralHUD_LateUpdate(void* self) {
    if (orig_GeneralHUD_LateUpdate) {
        orig_GeneralHUD_LateUpdate(self);
    }

    ProcessTouchAndMultiplayer();

    if (!IsValidPtr(self)) {
        return;
    }
    g_HUDActive = true;

    // 1. Get Camera
    void* cam = *(void**)((uintptr_t)self + OFFSET_HUD_MAINCAMERA);
    if (!IsValidPtr(cam) && Camera_get_main) {
        cam = Camera_get_main();
    }
    if (!IsValidPtr(cam) || !Camera_WorldToScreenPoint) {
        return;
    }

    // 2. Determine Gun Position, Bullet Speed, Gravity
    Vector3 gunPos(0, 0, 0);
    float bulletSpeed = 1000.0f;
    bool noGravity = false;
    bool foundTurret = false;

    void* parentSeat = *(void**)((uintptr_t)self + OFFSET_HUD_PARENTSEAT);
    if (IsValidPtr(parentSeat)) {
        void* aws = *(void**)((uintptr_t)parentSeat + OFFSET_SEAT_AWS);
        if (IsValidPtr(aws)) {
            void* activeTurret = nullptr;

            // Check selectable turrets
            auto selectable = *(monoList<void*>**)((uintptr_t)aws + OFFSET_AWS_SELECTABLE);
            if (IsValidPtr(selectable) && IsValidPtr(selectable->items) && selectable->getSize() > 0) {
                activeTurret = ((void**)selectable->items->vector)[0];
            }

            // Fallback to alwaysReadyTurrets
            if (!IsValidPtr(activeTurret)) {
                auto alwaysReady = *(monoList<void*>**)((uintptr_t)aws + OFFSET_AWS_ALWAYSREADY);
                if (IsValidPtr(alwaysReady) && IsValidPtr(alwaysReady->items) && alwaysReady->getSize() > 0) {
                    activeTurret = ((void**)alwaysReady->items->vector)[0];
                }
            }

            if (IsValidPtr(activeTurret)) {
                foundTurret = true;
                void* muzzle = *(void**)((uintptr_t)activeTurret + OFFSET_TURRET_MUZZLE);
                if (IsValidPtr(muzzle) && Transform_get_position) {
                    gunPos = Transform_get_position(muzzle);
                }

                void* ammoConfig = *(void**)((uintptr_t)activeTurret + OFFSET_TURRET_AMMOCONFIG);
                if (IsValidPtr(ammoConfig)) {
                    float spd = *(float*)((uintptr_t)ammoConfig + OFFSET_AMMO_SPEED);
                    if (spd > 50.0f) {
                        bulletSpeed = spd;
                    }
                }
                noGravity = *(bool*)((uintptr_t)activeTurret + OFFSET_TURRET_NOGRAVITY);
            }
        }
    }

    // Fallback: camera origin
    if (!foundTurret && Component_get_transform && Transform_get_position) {
        void* camTr = Component_get_transform(cam);
        if (IsValidPtr(camTr)) {
            gunPos = Transform_get_position(camTr);
        }
    }

    // 3. Read enemy units list
    auto enemyList = *(monoList<void*>**)((uintptr_t)self + OFFSET_HUD_ALLENEMIES);
    if (!IsValidPtr(enemyList) || !IsValidPtr(enemyList->items)) {
        return;
    }

    int totalEnemies = enemyList->getSize();
    if (totalEnemies <= 0 || totalEnemies > 250) {
        return;
    }

    int writeIdx = 1 - g_ActiveBufferIdx.load();
    int count = 0;
    float screenHeight = (glHeight > 0) ? (float)glHeight : 1080.0f;

    // 4. Calculate lead points for all airborne targets
    for (int i = 0; i < totalEnemies && count < MAX_TARGETS; i++) {
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
        if (leadScreen.Z <= 0.0f) {
            continue;
        }

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
// ImGui Drawing Loop (Called inside swapbuffers_hook on render thread)
// -----------------------------------------------------------------------------
void DrawMenu() {
    // 1. Pass captured touch coordinates into ImGui
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(g_TouchX.load(), g_TouchY.load());
    io.MouseDown[0] = g_TouchDown.load();

    // 2. Interactive Mod Menu Window
    ImGui::SetNextWindowPos(ImVec2(40.0f, 60.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 210.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("C-RAM MOD MENU", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "C-RAM Mod Menu v2.0");
        ImGui::Separator();

        // USER REQUESTED BUTTON: [ OPEN MULTIPLAYER ]
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.52f, 0.92f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.62f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.40f, 0.80f, 1.0f));

        if (ImGui::Button("[ OPEN MULTIPLAYER ]", ImVec2(310.0f, 65.0f))) {
            LOGI("USER PRESSED [ OPEN MULTIPLAYER ] BUTTON!");
            g_OpenMultiplayerRequested.store(true);
        }
        ImGui::PopStyleColor(3);

        if (g_MultiplayerOpened) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "Multiplayer Screen Launched!");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "Status: Ready");
        }

        ImGui::Separator();
        ImGui::Text("Lead Indicator: %s", g_HUDActive ? "ACTIVE" : "STANDBY");
    }
    ImGui::End();

    // 3. Lead Indicator ESP Overlay (in battle)
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
    if (!h) {
        h = dlopen("libil2cpp.so", RTLD_LAZY);
    }
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
    LOGI("C-RAM Mod Thread Started");

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

    // Resolve pointers
    Camera_get_main = (t_Camera_get_main)(g_Il2CppBase + RVA_CAMERA_MAIN);
    Camera_WorldToScreenPoint = (t_Camera_WorldToScreenPoint)(g_Il2CppBase + RVA_CAMERA_W2S);
    Component_get_transform = (t_Component_get_transform)(g_Il2CppBase + RVA_COMP_TRANSFORM);
    Transform_get_position = (t_Transform_get_position)(g_Il2CppBase + RVA_TRANS_POS);
    IUnit_GetUnitType = (t_IUnit_GetUnitType)(g_Il2CppBase + RVA_UNIT_TYPE);
    PhysicsObject_get_Velocity = (t_PhysicsObject_get_Velocity)(g_Il2CppBase + RVA_PHYS_VEL);

    Input_get_mousePosition = (t_Input_get_mousePosition)(g_Il2CppBase + RVA_INPUT_GET_MOUSE_POS);
    Input_GetMouseButton = (t_Input_GetMouseButton)(g_Il2CppBase + RVA_INPUT_GET_MOUSE_BTN);
    MpEntry_OnClicked = (t_MpEntry_OnClicked)(g_Il2CppBase + RVA_MP_ENTRY_CLICKED);

    // 1. Hook GeneralHUD.LateUpdate for Battle ESP
    void* targetHUDMethod = (void*)(g_Il2CppBase + RVA_HUD_LATEUPDATE);
    int hudHookRes = DobbyHook(targetHUDMethod, (void*)hook_GeneralHUD_LateUpdate, (void**)&orig_GeneralHUD_LateUpdate);
    LOGI("DobbyHook GeneralHUD.LateUpdate (%p) returned: %d, orig=%p", targetHUDMethod, hudHookRes, orig_GeneralHUD_LateUpdate);

    // 2. Hook EventSystem.Update for Global UI & Main Menu Multiplayer trigger
    void* targetEventSystemMethod = (void*)(g_Il2CppBase + RVA_EVENTSYSTEM_UPDATE);
    int esHookRes = DobbyHook(targetEventSystemMethod, (void*)hook_EventSystem_Update, (void**)&orig_EventSystem_Update);
    LOGI("DobbyHook EventSystem.Update (%p) returned: %d, orig=%p", targetEventSystemMethod, esHookRes, orig_EventSystem_Update);

    LOGI("C-RAM Lead & Multiplayer Mod Hooks Installed Successfully!");
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