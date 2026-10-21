C-ram lead indicator mod

Small native mod for mobile c-ram ciws, which calculating and drawing lead indicator



<img width="1280" height="500" alt="document_5292213038038951572" src="https://github.com/user-attachments/assets/5c32979b-4020-474b-9616-e0abc5288193" />

## Features

- Lead indicator for air targets (planes, helicopters, drones, missiles) with flight time and range
- Native game target diamonds restored after respawn in multiplayer
- AIM-120 AMRAAM for western jets (under-wing pylons, single player only)
- Cockpit arcade steering, landing gear toggle, unlimited livery decals, AC-130 recoil suppression

## Build

GitHub Actions builds `libModMenu.so` (arm64-v8a, NDK r25c) on every push and uploads it as the `mod-binaries` artifact.

Sources: `app/src/main/jni/Main/Main.cpp`, AIM-120 mesh: `app/src/main/jni/Include/Amraam.h` (generated from glTF).

AIM-120 model: "AIM-120 AMRAAM" by Snicks Game Development, CC BY 4.0, https://sketchfab.com/3d-models/aim-120-amraam-ba499a976413467ab7bd18d3c29e40b4
