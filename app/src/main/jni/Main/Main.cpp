//
// C-RAM Air Target Lead Indicator Mod
//

#include <pthread.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../Include/KittyMemory/MemoryPatch.h"
#include "../Include/ImGui.h"
#include "../Include/Drawing.h"
#include "../Include/Unity.h"
#include "../Include/Logger.h"

// -----------------------------------------------------------------------------
// RVAs from dump.cs (Relative to libil2cpp.so base)
// -----------------------------------------------------------------------------
#define RVA_CAMERA_MAIN        0x82D7D50
#define RVA_CAMERA_W2S         0x82D72A4
#define RVA_COMP_TRANSFORM     0x834A0F4
#define RVA_TRANS_POS          0x8365D9C
#define RVA_UNIT_TYPE          0x3F54748
#define RVA_PHYS_VEL           0x3EACAA4
#define RVA_HUD_LATEUPDATE     0x3F16414

// -----------------------------------------------------------------------------
// Field Offsets from dump.cs
// -----------------------------------------------------------------------------
#define OFFSET_HUD_PARENTSEAT   0x20
#define OFFSET_HUD_MAINCAMERA   0xE8
#define OFFSET_HUD_ALLENEMIES   0x140

#define OFFSET_SEAT_AWS         0x28

#define OFFSET_AWS_ALWAYSREADY  0x28
#define OFFSET_AWS_SELECTABLE   0x80

#define OFFSET_TURRET_MUZZLE    0xD8
#define OFFSET_TURRET_AMMOCONFIG 0xC0
#define OFFSET_TURRET_NOGRAVITY 0xFC

#define OFFSET_AMMO_SPEED       0x24

#define OFFSET_UNIT_ISALIVE     0xC4

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

static t_Camera_WorldToScreenPoint Camera_WorldToScreenPoint = nullptr;
static t_Camera_get_main Camera_get_main = nullptr;
static t_Component_get_transform Component_get_transform = nullptr;
static t_Transform_get_position Transform_get_position = nullptr;
static t_IUnit_GetUnitType IUnit_GetUnitType = nullptr;
static t_PhysicsObject_get_Velocity PhysicsObject_get_Velocity = nullptr;
static t_GeneralHUD_LateUpdate orig_GeneralHUD_LateUpdate = nullptr;

// -----------------------------------------------------------------------------
// Global State & Settings
// -----------------------------------------------------------------------------
static void* g_GeneralHUD = nullptr;
static uintptr_t g_Il2CppBase = 0;

static bool g_LeadIndicatorEnabled = true;
static bool g_DrawLines = true;
static bool g_DrawDistanceText = true;
static float g_LeadCircleRadius = 14.0f;
static float g_ManualSpeedOverride = 0.0f; // 0.0f = auto-detect from turret

// Check if pointer looks like valid userspace memory
inline bool IsValidPtr(const void* ptr) {
    uintptr_t p = (uintptr_t)ptr;
    return p > 0x100000 && p < 0x7fffffffff;
}

// -----------------------------------------------------------------------------
// GeneralHUD.LateUpdate Hook
// -----------------------------------------------------------------------------
void hook_GeneralHUD_LateUpdate(void* self) {
    if (self) {
        g_GeneralHUD = self;
    }
    if (orig_GeneralHUD_LateUpdate) {
        orig_GeneralHUD_LateUpdate(self);
    }
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
// ImGui Drawing Loop (Called on every frame)
// -----------------------------------------------------------------------------
void DrawMenu() {
    // 1. Mod Menu Control Window
    ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("C-RAM Lead Mod", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Checkbox("Enable Lead Indicator", &g_LeadIndicatorEnabled);
        ImGui::Checkbox("Draw Target Lines", &g_DrawLines);
        ImGui::Checkbox("Show Distance & Time", &g_DrawDistanceText);
        ImGui::SliderFloat("Circle Radius", &g_LeadCircleRadius, 8.0f, 26.0f);
        ImGui::SliderFloat("Speed Override (0=Auto)", &g_ManualSpeedOverride, 0.0f, 2000.0f, "%.0f m/s");

        if (g_GeneralHUD) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: HUD Active");
        } else {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Status: Waiting for game...");
        }
    }
    ImGui::End();

    // 2. Render Lead Circles if enabled
    if (!g_LeadIndicatorEnabled || !g_GeneralHUD || !IsValidPtr(g_GeneralHUD)) {
        return;
    }

    // Get Camera
    void* cam = *(void**)((uintptr_t)g_GeneralHUD + OFFSET_HUD_MAINCAMERA);
    if (!IsValidPtr(cam) && Camera_get_main) {
        cam = Camera_get_main();
    }
    if (!IsValidPtr(cam) || !Camera_WorldToScreenPoint) {
        return;
    }

    // Determine Gun Position & Bullet Speed
    Vector3 gunPos(0, 0, 0);
    float bulletSpeed = 1000.0f; // Default 1000 m/s fallback
    bool noGravity = false;
    bool foundTurret = false;

    void* parentSeat = *(void**)((uintptr_t)g_GeneralHUD + OFFSET_HUD_PARENTSEAT);
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

    // Manual override if set
    if (g_ManualSpeedOverride > 50.0f) {
        bulletSpeed = g_ManualSpeedOverride;
    }

    // If turret muzzle wasn't resolved, use camera position as gun origin
    if (!foundTurret && Component_get_transform && Transform_get_position) {
        void* camTr = Component_get_transform(cam);
        if (IsValidPtr(camTr)) {
            gunPos = Transform_get_position(camTr);
        }
    }

    // Get enemy units list
    auto enemyList = *(monoList<void*>**)((uintptr_t)g_GeneralHUD + OFFSET_HUD_ALLENEMIES);
    if (!IsValidPtr(enemyList) || !IsValidPtr(enemyList->items)) {
        return;
    }

    int totalEnemies = enemyList->getSize();
    if (totalEnemies <= 0 || totalEnemies > 250) {
        return;
    }

    float screenHeight = ImGui::GetIO().DisplaySize.y;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw) return;

    // Iterate through ALL air targets (1, 7, 20, etc.)
    for (int i = 0; i < totalEnemies; i++) {
        void* target = ((void**)enemyList->items->vector)[i];
        if (!IsValidPtr(target)) continue;

        // Check if target is alive
        bool isAlive = *(bool*)((uintptr_t)target + OFFSET_UNIT_ISALIVE);
        if (!isAlive) continue;

        // Filter: UnitType.Air = 3, UnitType.Heli = 6
        if (IUnit_GetUnitType) {
            int unitType = IUnit_GetUnitType(target);
            if (unitType != 3 && unitType != 6) {
                continue;
            }
        }

        // Get target position
        void* tr = Component_get_transform ? Component_get_transform(target) : nullptr;
        if (!IsValidPtr(tr) || !Transform_get_position) continue;
        Vector3 targetPos = Transform_get_position(tr);

        // Get target velocity
        Vector3 targetVel(0, 0, 0);
        if (PhysicsObject_get_Velocity) {
            targetVel = PhysicsObject_get_Velocity(target);
        }

        // Calculate lead point
        Vector3 leadPos(0, 0, 0);
        float flightTime = 0.0f;
        float distance = 0.0f;

        if (!CalculateLead(targetPos, targetVel, gunPos, bulletSpeed, noGravity, leadPos, flightTime, distance)) {
            continue;
        }

        // Project lead position to screen
        Vector3 leadScreen = Camera_WorldToScreenPoint(cam, leadPos);
        if (leadScreen.Z <= 0.0f) {
            continue; // Behind camera
        }

        // Project target position to screen for connecting line
        Vector3 targetScreen = Camera_WorldToScreenPoint(cam, targetPos);

        ImVec2 leadPoint(leadScreen.X, screenHeight - leadScreen.Y);
        ImU32 circleColor = IM_COL32(255, 50, 50, 230);
        ImU32 centerColor = IM_COL32(255, 255, 255, 255);
        ImU32 lineColor = IM_COL32(255, 200, 50, 150);

        // 1. Lead Circle
        draw->AddCircle(leadPoint, g_LeadCircleRadius, circleColor, 24, 2.5f);
        draw->AddCircleFilled(leadPoint, 2.5f, centerColor);

        // Crosshair reticle
        float crossSize = g_LeadCircleRadius * 0.5f;
        draw->AddLine(ImVec2(leadPoint.x - crossSize, leadPoint.y), ImVec2(leadPoint.x + crossSize, leadPoint.y), circleColor, 1.5f);
        draw->AddLine(ImVec2(leadPoint.x, leadPoint.y - crossSize), ImVec2(leadPoint.x, leadPoint.y + crossSize), circleColor, 1.5f);

        // 2. Connecting Line from Aircraft to Lead Circle
        if (g_DrawLines && targetScreen.Z > 0.0f) {
            ImVec2 targetPoint(targetScreen.X, screenHeight - targetScreen.Y);
            draw->AddLine(targetPoint, leadPoint, lineColor, 1.5f);
        }

        // 3. Distance & Flight Time Text
        if (g_DrawDistanceText) {
            char textBuf[64];
            snprintf(textBuf, sizeof(textBuf), "%.0fm (%.1fs)", distance, flightTime);
            draw->AddText(ImVec2(leadPoint.x + g_LeadCircleRadius + 4, leadPoint.y - 7), IM_COL32(255, 255, 255, 230), textBuf);
        }
    }
}

// -----------------------------------------------------------------------------
// Hook Initialization Thread
// -----------------------------------------------------------------------------
void* thread(void*) {
    LOGI("C-RAM Lead Mod Thread Started");

    // Initialize ImGui EGL Hook
    initModMenu((void*)DrawMenu);

    // Wait until libil2cpp.so is loaded into memory
    while (!g_Il2CppBase) {
        g_Il2CppBase = getBaseAddress("libil2cpp.so");
        if (!g_Il2CppBase) {
            usleep(250000); // 250ms
        }
    }

    LOGI("libil2cpp.so found at: %p", (void*)g_Il2CppBase);
    sleep(2); // Give game time to initialize IL2CPP runtime

    // Resolve function addresses
    Camera_get_main = (t_Camera_get_main)(g_Il2CppBase + RVA_CAMERA_MAIN);
    Camera_WorldToScreenPoint = (t_Camera_WorldToScreenPoint)(g_Il2CppBase + RVA_CAMERA_W2S);
    Component_get_transform = (t_Component_get_transform)(g_Il2CppBase + RVA_COMP_TRANSFORM);
    Transform_get_position = (t_Transform_get_position)(g_Il2CppBase + RVA_TRANS_POS);
    IUnit_GetUnitType = (t_IUnit_GetUnitType)(g_Il2CppBase + RVA_UNIT_TYPE);
    PhysicsObject_get_Velocity = (t_PhysicsObject_get_Velocity)(g_Il2CppBase + RVA_PHYS_VEL);

    // Hook GeneralHUD.LateUpdate to capture HUD and enemies list
    void* targetHUDMethod = (void*)(g_Il2CppBase + RVA_HUD_LATEUPDATE);
    DobbyHook(targetHUDMethod, (void*)hook_GeneralHUD_LateUpdate, (void**)&orig_GeneralHUD_LateUpdate);

    LOGI("C-RAM Lead Mod Hooks Successfully Installed!");
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