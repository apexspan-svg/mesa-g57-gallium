# Panfrost Gallium on kbase (Mali-G57 / Valhall JM)

Desktop OpenGL (and GLES) via Mesa Gallium **panfrost** on Arm Mali GPUs
running the vendor **kbase** kernel driver (`/dev/mali0`) — no root, no
custom kernel, no mainline `panfrost.ko`.

Target: **Mali-G57 MC2** (Valhall v9, Job Manager), e.g. Dimensity 6300.
Tree: Mesa 26.3.0-devel + kbase JM backend + Gallium submit/fence/loader
ports. Vulkan (PanVK) files remain in-tree from the base but this project's
work is the **Gallium** driver.

## Status (hardware-verified on Dimensity 6300)

- EGL surfaceless init on `/dev/mali0` via `MESA_KBASE_NODE` — PASS
- Clear, flat-shaded triangle, textured quad (texel-exact) — PASS
- App-side SHM present to X11 window (triangle + textured, eyeball-verified)
- GLX device hook (`x11_dri3_open` kbase fallback) — in tree, CI-green
- Not yet: in-driver swap present (real `glxgears`), async submit, X11 WSI

## Test (Termux, NDK artifact)

```
unzip panfrost-g57-kbase-android.zip -d kbasegl
cc tests/egl_kbase_gles_triangle.c -ldl -o egl_kbase_gles_triangle
MESA_KBASE_NODE=/dev/mali0 PAN_EXPERIMENTAL_KBASE_GL=1 \
  EGL_LIB=./kbasegl/libEGL_mesa.so LIBGL_DRIVERS_PATH=./kbasegl \
  ./egl_kbase_gles_triangle   # needs libgallium_dri.so as panfrost_dri.so
```

See `tests/`, `G57_NOTES.md`.

## Builds (GitHub Actions)

- `android-gallium-kbase`: NDK r29 cross (`libEGL_mesa`, `libGLESv2_mesa`,
  `libgallium_dri`) — green, device-tested.
- `termux-native-gallium`: true `$PREFIX` binaries via termux-docker —
  green, layout-verified.

## Credits

Mesa/Collabora/Arm Panfrost + PanVK developers; apexspan (kbase JM base);
JimVulkan (traced submit values, async/event design, loader approach);
HapplyCardo (G57 faults); icecream95 panfork lineage. See `G57_NOTES.md`.
MIT, like Mesa.
