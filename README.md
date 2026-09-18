# C-ram lead indicator mod
Small native mod for mobile c-ram ciws, which calculating and drawing lead indicator

<img width="1280" height="500" alt="document_5292213038038951572" src="https://github.com/user-attachments/assets/5c32979b-4020-474b-9616-e0abc5288193" />

# How it works

### 1. Injection (DT_NEEDED)
In the libmain.so header, the DT_NEEDED dependency entry (which pointed to liblog.so) has been patched to libmod.so. The Android system bootloader itself loads the mod library at startup, and the constructor in .init_array launches a separate thread.

### 2. Hooks in memory
In RAM, the first 16 bytes of functions are changed via mprotect on the ARM64 trampoline:
- In libEGL.so, eglSwapBuffers is intercepted.
- In libil2cpp.so, GeneralHUD.LateUpdate is intercepted at offset 0x3F16414.

### 3. Coordinate transmission
Logic and rendering are decoupled:
- The UnityMain thread in the LateUpdate hook takes the 3D coordinates of the targets, converts them to 2D using Camera.WorldToScreenPoint, and stores them in the shadow buffer in .bss.
- The threads are swapped via std::atomic<int>, so there are no mutexes here and the game doesn’t lag.

### 4. Rendering
In the eglSwapBuffers hook, before rendering the frame:
- OpenGL settings are saved to avoid breaking anything..
- Dear ImGui takes screen coordinates from the buffer and generates a grid of warning markers..
- Through glDrawElements, the overlay is drawn directly over the game frame, the OpenGL settings are restored, and the original eglSwapBuffers is called.


# Calculation of the intercept point

We take the data from the game:
- P_gun — the position of the gun barrel (turret.muzzle.position)
- P_t — the coordinates of the target
- V_t — the target’s velocity (target.Velocity)
- V_b — the projectile’s speed (turret.ammoConfig.speed)
- R = P_t - P_gun — the vector from the gun to the target

The projectile’s flight time to the intercept point $t$ is found using a quadratic equation:
$$a \cdot t^2 + b \cdot t + c = 0$$

Where the coefficients are:
- $a = |\vec{V}_t|^2 - V_b^2$
- $b = 2 \cdot (\vec{R} \cdot \vec{V}_t)$
- $c = |\vec{R}|^2$

Counting the discriminant:
$$D = b^2 - 4ac$$

If $D < 0$, the projectile will not physically catch up with the target on the current course.

We take the minimum positive time $t > 0$:
$$t = \frac{-b - \sqrt{D}}{2a}$$
(if it turns out to be $\le 0$, take the second root of $\frac{-b+ \sqrt{D}}{2a}$).

We calculate the world position of the lead point:
$$\vec{P}_{lead} = \vec{P}_t + \vec{V}_t \cdot t$$

If the projectiles have ballistics (we take into account the fall due to gravity $\vec{g}$):
$$\vec{P}_{lead} = \vec{P}_t + \vec{V}_t \cdot t - \frac{1}{2}\vec{g}t^2$$

We retrieve the final 2D point for the screen via the call:
Camera.main.WorldToScreenPoint(P_lead)
