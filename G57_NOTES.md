# Mali-G57 notes (hardware-earned, from sister projects)

## Verified in our tree
- Tiler hierarchy mask: upstream `pan_select_tiler_hierarchy_mask` — no
  `0xFE` Valhall-tuning present. The `0x58 DATA_INVALID_FAULT` class
  HapplyCardo hit does not apply (`pan_jm.c`, `pan_tiler.c` checked).
- v9 `MALLOC_VERTEX` VARYING block: upstream rewrite, correct
  `if (!secondary_shader) continue` + `jm_emit_shader_env`. The panfork-23
  brace/`vary.shader` bug (HapplyCardo `4c42d497`) has no equivalent here.
- Atom completion: synchronous submit+wait makes 8-bit atom-ID wrap
  impossible (one in flight). HapplyCardo's drain-before-wrap concern
  applies only to async designs — revisit in the async port.

## Adopt when relevant
- Fence seq comparison must be `>=`, not `>` (HapplyCardo `4c42d497`:
  completion at exactly the waited value must signal, else waits time
  out). Applies to the planned async event layer, not the current
  synchronous submit.
- AFBC: disable on G57 kbase until proven (`PANVK_NO_AFBC=1` precedent in
  apexspan; `PAN_BASE_QUIRK_NO_AFBC` in HapplyCardo). Suspect for any
  future `DATA_INVALID_FAULT` in textured scenes.
- Varying (textured) draws are the next untested v9 path: our PASS covers
  clear + flat-shaded triangle only.

## kbase version notes
- Atom stride is kernel-version dependent (BossDrk, G52/r49: 56 bytes vs
  the usual 64). Our submit defaults to 64 (proven on JM 11.38) with
  `PAN_KBASE_ATOM_STRIDE=56|64|72` override; FristOneRR auto-detects.

## Sources
- HapplyCardo/mesa-Panfork-android `bifrost-g57` (`d8cb29f9`, `4c42d497`):
  G57 bring-up on Dimensity 820, glmark2 + glxgears ~287 FPS.
- JimVulkan/mali-panfrost: traced `core_req` values, async/event design.
- apexspan/mesa-panvk-mali-g57: PanVK JM backend, X11 WSI reference.
