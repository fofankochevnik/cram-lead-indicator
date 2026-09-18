//
// C-RAM ImGui EGL Hook
//

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <unistd.h>

#include "ImGui/imgui.h"
#include "Roboto-Regular.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"

#include "Utils.h"
#include "Dobby/dobby.h"
#include "Logger.h"

void (*menuAddress)() = nullptr;

using swapbuffers_orig = EGLBoolean (*)(EGLDisplay dpy, EGLSurface surf);
EGLBoolean swapbuffers_hook(EGLDisplay dpy, EGLSurface surf);
swapbuffers_orig o_swapbuffers = nullptr;

bool isInitialized = false;
int glWidth = 0;
int glHeight = 0;

void *initModMenu(void *menu_addr) {
    menuAddress = (void (*)())menu_addr;
    
    // Wait until libEGL.so is loaded
    while (!isLibraryLoaded("libEGL.so")) {
        usleep(100000); // 100ms
    }

    void *swapBuffers = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!swapBuffers) {
        void *hEgl = dlopen("libEGL.so", RTLD_NOW);
        if (hEgl) {
            swapBuffers = dlsym(hEgl, "eglSwapBuffers");
        }
    }
    if (!swapBuffers) {
        swapBuffers = (void *)eglGetProcAddress("eglSwapBuffers");
    }

    if (swapBuffers) {
        DobbyHook(swapBuffers, (void *)swapbuffers_hook, (void **)&o_swapbuffers);
        LOGI("ImGUI eglSwapBuffers hooked successfully at %p", swapBuffers);
    } else {
        LOGE("Failed to find eglSwapBuffers!");
    }

    return nullptr;
}

void setupMenu() {
    if (isInitialized) return;

    auto ctx = ImGui::CreateContext();
    if (!ctx) {
        LOGI("Failed to create ImGui context");
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)glWidth, (float)glHeight);
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = nullptr;

    // Setup Platform/Renderer backends
    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init("#version 300 es");

    ImFontConfig font_cfg;
    font_cfg.SizePixels = 24.0f;
    io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, sizeof(Roboto_Regular), 24.0f, &font_cfg);

    ImGui::GetStyle().ScaleAllSizes(1.5f);

    isInitialized = true;
    LOGI("ImGui setup done.");
}

void internalDrawMenu(int width, int height) {
    if (!isInitialized || !menuAddress) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(width, height);
    ImGui::NewFrame();

    menuAddress();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

EGLBoolean swapbuffers_hook(EGLDisplay dpy, EGLSurface surf) {
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0) {
        glWidth = w;
        glHeight = h;
        setupMenu();
        internalDrawMenu(w, h);
    }

    if (o_swapbuffers) {
        return o_swapbuffers(dpy, surf);
    }
    return EGL_TRUE;
}
