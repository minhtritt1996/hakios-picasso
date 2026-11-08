# HakiOS picasso — Display & GPU bring-up plan

Date: 2026-11-07. Research-only plan (no code changed, phone not booted).
Scope: display (panel/DSI/backlight/framebuffer) + hardware GPU (Adreno 620) on
Redmi K30 5G (picasso, SM7250), HakiOS branch muse/wifi-bringup-t1-t5.
Separate track from the Wi-Fi bring-up (WMI_INIT rings); do not touch Wi-Fi files.

## Ground truth (do not overclaim)

- Display does NOT work yet. The 2026-11-04 GUI boot only shows SpringBoard
  check-in in the log; the homescreen was NEVER seen on the phone screen.
- The only verified GPU-side pixel output is the Adreno step-22 color bands
  written into the framebuffer (log claim; never confirmed by photo/observation).
- device.conf claims `display partial` / `gpu partial` / `wifi works` — treat as
  aspirational/stale; only log- and photo-backed evidence counts.

## Sources & caveats

- Five parallel research tracks (docs, audit, panel, GPU, references), 2026-11-07.
  Every claim below cites a file actually read.
- Caveat: the tracks could NOT reach k60 (SSHPASS was not available inside the
  research subagents). All repo reads used local clones: main @ 15983f3 and
  branch muse/wifi-bringup-t1-t5 @ c8a3813 (last pushed commit). No display/GPU
  file differs between them (verified by the audit track). k60-only state
  (note.md §3.100+, uncommitted rings 81-117, build/*.log, build/out/,
  fw-gpu/ staging) is UNVERIFIED — re-check on k60 before acting on anything
  that depends on it.
- Upstream docs: oosexgang/hakios-xnu-kernel docs (porting/index.md,
  reference/metal.md, reference/drivers.md) as crawled 2026-11-07; pinned
  upstream hakios-xnu-kernel 96ffbc5, hakios-metal 7025778.

## 1. How HakiOS display/GPU works (architecture)

### Display — UEFI GOP framebuffer; no DRM/KMS/DSI/panel driver

1. UEFI (Mu-Silicium picassoPkg/LitoPkg) leaves a GOP framebuffer 1080x2400.
   HakiOS has NO panel/DSI init of its own; the panel is left exactly as the
   stock bootloader/UEFI initialized it. ("The display shows UEFI's
   framebuffer." — docs/porting/index.md:71)
2. xnuloader stages the kernel in RAM and boots; the kernel sees the
   framebuffer via struct vc_info vinfo.
3. MiniIOMobileFramebuffer is the whole display driver: common upstream
   xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp, started early in
   GenericARMPE::start(), publishes nub disp0 under /arm-io, registers
   IOMobileFramebuffer. Refuses to start without a usable boot FB
   (vinfo.v_baseaddr != 0, 32-bit depth). Picasso changes live in
   patches/0001 (selectors 15/22/29/33/57/74/79, CanvasWidth/CanvasHeight,
   vsync timer, presenter thread, shadow-diff copy).
4. Userspace: backboardd finds FB Count = 1, opens
   IOMobileFramebufferUserClient, StartWindowServer creates
   CAWindowServerDisplay 1125x2436 scale 3. QuartzCore/WindowServer swaps
   frames through IOSurfaces; kernel show()/drawRows() writes rows into the
   panel FB (fb_beam, fb_hz, fb_threads).
5. Selector 74 (kFBMethodSupportedFrameInfo) deliberately returns 0 by
   default — returning >0 makes QuartzCore expect async kFBNotifyFrameInfo
   (type 3) and its queue grows unboundedly. Opt-in via fb_frameinfo=1.
6. Backlight/ALS is FAKED, not driven: no ALS driver, no backlight hardware
   path. inject.c brightness_fixes() skips -[BLControl waitForALSArrival]
   and hooks BrightnessSystemClient setProperty:forKey: to force
   DisplayBrightnessFactor 0 -> 1.0, and hooks -[CAWindowServerDisplay
   setBlanked:] to force NO. Caveat: the factor hook NEVER logged a hit —
   backboardd sets the factor through BKDisplayBrightnessController instead.

### GPU — own XNU driver + Mesa in userspace

- Kernel: drivers/gpu/hakios.adreno.c (step-by-step bring-up, adreno=N runs
  steps 0..N) + drivers/gpu/hakios.gpu.cpp (HakiosGPU "SweetGPU" IOKit
  service, DRM-like ABI). CONFIG_HAKIOS_ADRENO=y (configs/picasso.config:13);
  CONFIG_GPU deliberately NOT set (Linux DRM symbol).
- Firmware (vendor, from the phone, never in repo): fw-gpu/ a620_zap.mdt +
  a620_zap.b00/.b01/.b02 (TrustZone Zap, PAS ID 13), a650_gmu.bin (GMU),
  a630_sqe.fw (SQE). Pulled from /vendor/firmware via pull-firmware.sh.
  Detection: if a620_zap.mdt exists, GMU=a650_gmu.bin else a620_gmu.bin;
  SQE=a650_sqe.fw else a630_sqe.fw else a620_sqe.fw; only .b02 loaded.
- Steps (anchored): 5 clock measure, 6 GPU block/SMMU probe, 12 zap
  signature, 13 GMU address space, 14 GMU FW load, 15 GMU start, 16 GMU
  queues (RSCC/PDC), 17 core power/speed vote (275/400/500 MHz), 18 core
  address space, 19 zap load, 20 core start (SQE/CP), 21 core commands,
  22 step_frame 2D-blitter test (3 color bands orange/white/blue).
- Blocks: GPUCC 0x3d90000, PDC 0x0b290000, PDC-seq 0x0b490000, RSCC
  0x3de0000, QFPROM 0x780000 (speed bin 0x1ea, gaming bin 0x212), GMU clock
  200 MHz (GPLL0/3), zap mem 0x8bc16000 (DT reserves 0x8bc15400..0x8bc17400).
- Userspace default path (only one verified on picasso): OpenGL ES 3.2 via
  Mesa 25.0.7 freedreno (FD620) through libhakios.gl.dylib, loaded by
  CoreAnimation in backboardd; build with GL=1. hakios-metal path: unproven
  on picasso (chip-ID gate refuses unknown Adreno; tables are for
  0x06030500; all Metal measurements are redwood/Adreno 642L). Turnip/Vulkan:
  claim only, no evidence in picasso repo.
- Boot args: adreno=N, gpu=1, adreno_keep=1, mfb=1, ca_vbl=1, fb_hz=,
  fb_beam=, fb_threads=, fb_frameinfo=1, metal_stub=1, adreno_zap=0. GUI
  isolation cmdline: mfb=1 wifi=0 modem=0 adsp=0 sound=0 mic=0 bt=0
  adreno=22 gpu=1 adreno_keep=1 ca_vbl=1. Kernel cmdline cap ~607 chars —
  order matters.

## 2. Current state (audit of branch muse/wifi-bringup-t1-t5 @ c8a3813)

Present: patches/0001 (display, 2840 lines), patches/0003 (Adreno 620, 3522
lines), patches/0004 (userland inject incl. brightness_fixes + setBlanked
unblank hook + dumpfb), patches/0006 (hakios-metal), device.conf, configs,
soc.args (no display entries), prebuilt/device-tree.dtb (Android DTB blob;
contains qcom,mdss_mdp, mdss_dsi_phy0/1, gpucc@3d90000, gpu-opp-table,
pil_gpu_region@8bc15400; NO panel node found by strings),
uefi/.../picasso.dsc (USE_CUSTOM_DISPLAY_DRIVER=0, FB 1080x2400x32, MDSS
0x0AE00000 size 0x134000), tools/boot_gui_full.py,
tools/boot_wifi_gui_test.py, tools/usb_screenshot.py + raw2bmp.py (zero
captured evidence; usb_screenshot hardcodes /home/tritm/... — rule
violation), drivers/input Novatek touch (broken, SPI rc=-5),
pull-firmware.sh.

Missing: any *.dts* source; drivers/display/ and drivers/gpu/ (drivers/ has
only bluetooth/input/wifi); any DSI/MDSS/panel-init code (zero in repo);
panel XML blobs (uefi RAW.inc references ~10 Panel_*.xml, none present;
uefi/files/Binaries/picasso/ has only README.md); backlight hardware driver
(no ALS, no PMIC/WLED control); GPU firmware in repo (by design — fw-gpu/
staged from phone; clean payload has broken/empty fw symlinks per note.md
L307-308); any surviving GUI boot log or screenshot (.bmp/.raw) in the repo.

### What 2026-11-04 actually showed (note.md §2-§3; raw logs do not survive)

- 15:28 gpu_gui.log: backboardd ran selectors, loaded libhakios.gl.dylib,
  created CAWindowServerDisplay 1125x2436 scale 3, then hung at
  "[Backlight] instantiating BrightnessSystem" (waitForALSArrival deadlock;
  brightness_fixes did not exist yet).
- 21:11 (after brightness_fixes + root.apfs repack): SpringBoard check-in
  ("Telling com.apple.springboard that it can start immediately",
  checkedIn: YES), then immediately "[Display] Set Blanked: YES",
  "Setting CoreBrightness backlight factor:0.000000", setBlanked hook
  forcing 0, "Set BrightnessSystem property:DisplayBrightnessFactor to:0",
  "TouchEvents canceling events for suppression reason: backlight dim/off".
  Screen state after check-in NOT confirmed. The factor hook never fired;
  the planned fix (hook BKDisplayBrightnessController, call
  -[SBBacklightController turnOnScreenFullyWithBacklightSource:]) was never
  recorded as done.
- 2026-11-06 finding: with mfb=0 ca_vbl=0 adreno=0 gpu=0, init never runs at
  all — display/GPU init is on the boot path, not optional.

## 3. Panel facts (G7A, from the stock DTB in the repo)

- Panel: qcom,mdss_dsi_g7a_37_02_0a_dsc_video ("xiaomi 37 02 0a VIDEO
  PANEL"), 6.67" IPS LCD, 1080x2400, 120 Hz default (DFPS 120/90/60/50/30),
  HDR on. Fallback sibling g7a_37_02_0b (identical timings);
  g7a_36_02_0c (same res/rate, different porches); j9_38_0a_0a (60 Hz, no
  DSC, bl_ctrl_dcs). Panel vendor/DDIC NOT identified anywhere in DTB/repo
  — "Tianma/Novatek" is unverified for the panel; Novatek NT36672C is the
  TOUCH controller only.
- DSI: 4 lanes, DSI0 video mode, burst_mode, 24bpp RGB, DSC 1.1 (slice
  540x20, 2 slices/pkt, 8bpc, 8bpp), pixel clock ~346.5 MHz (~0.62
  Gbps/lane). DSI ctrl qcom,dsi-ctrl-hw-v2.4 @ 0xae94000, PHY
  qcom,dsi-phy-v4.1 @ 0xae94400 (+ dyn-refresh 0xae94200).
- Timings @120Hz: H pulse 12, back porch 40, front porch 40; V pulse 2,
  back porch 30, front porch 32. PHY timing bytes 00 1c 07 07 23 21 07 07
  05 02 04 00 18 17.
- Power/reset: vddio 1.896V, avdd/avee 4.6-6.0V (LAB/IBB); reset PMIC GPIO3
  seq (1,5ms),(0,1ms),(1,10ms); ESD IRQ TLMM GPIO55.
- ON command: 1531 bytes / 170 packets, all dtype 0x15 DCS long writes of
  2-byte pairs behind 0xFF page selects (pages 0x20,0x23,0x24,0x26,0x27,
  0x2A,0x2C,0xC0,0xD0,0xE0,0xF0,0x10), then 0x39 packet 51 0B 6C
  (brightness 2924), Sleep Out 0x11 +70ms, Display On 0x29, post-on page
  writes. OFF: 54 bytes HS mode, Display Off 0x28, Sleep In 0x10 +70ms.
- Backlight: PMIC WLED (PM8150L, SPMI 0xd800/0xd900, label=backlight),
  12-bit 0-4095 (min 2), default 2924 (71.4%), CABC on. NOT DCS-controlled
  (j9 variant uses bl_ctrl_dcs; g7a uses bl_ctrl_wled).
- Critical: panel is VIDEO mode + DSC — the DPU/DSI must stream
  continuously. If anything resets MDSS or stops the stream, the panel goes
  black even with backlight on. No DSI init is needed for first light:
  stock ABL/XBL already initialized the panel before UEFI; xnuloader sees
  a GOP FB at 1080x2400.

## 4. GPU facts (Adreno 620, SM7250)

- Same firmware set as ios-redfin (same SoC/GPU): a650_gmu.bin,
  a650_sqe.fw, a620_zap.mdt/.bNN. Picasso tries a650_sqe.fw first, then
  a630_sqe.fw — consistent. Do NOT mix in flame's a640_gmu.bin/a640_zap
  (SM8150) or a630_gmu.bin (does not exist for this phone).
- Sunfish (Adreno 618) caution: GMU boots but CM3 faults at HFI_CTRL_INIT
  (SMMU bus fault reading the HFI table) — same A6xx family, different
  GMU/HFI behaviour per chip; do not assume values transfer without probing.
- GMU details (patch 0003): chip ID 0x06020000; ITCM 0x00000000 / DTCM
  0x10004000, 16 KB each, zeroed before load; GMU address space SFR
  0x60001000 (4 KB), log 0x60002000 (16 KB), HFI table 0x60006000 (4 KB,
  3 queues x 1024 words); GMU SMMU stream 5; start polls
  GMU_CM3_FW_INIT_RESULT for GMU_STARTED 0x100 (accepts 0x901 / low byte 1).
- Clocks/power: GMU clock 200 MHz (GPLL0/3 via GPUCC_GMU_RCGR); hub clock
  programming removed for SM7250; enabled cxo_aon, cxo, ahb, cx_gmu,
  SMMU-vote CBCRs; CX+GX GDSCs via GPUCC; step 17 perf table 0/275/400/500
  MHz, vote index 1 default; RPMh RSCC+PDC sequences, bus votes SH0/MC0/
  ACV/CN0 via HFI_BW_TABLE; 3D-core SMMU context 0, streams 0/1/0x400/0x401.
- Userspace: Mesa 25.0.7 freedreno FD620, OpenGL ES 3.2, via
  libhakios.gl.dylib in backboardd (GL=1 build). HakiosGPU/SweetGPU service
  registered for Mesa/freedreno. hakios-metal: unproven on picasso.
  Turnip/Vulkan: claim only.

## 5. What to copy from references

- ios-redfin (Pixel 5, SM7250, display+GPU work; known-good rk5a
  2026-10-30: ~55-64 fps scroll, GPU 5.4 ms/frame, touch->screen p50
  56 ms):
  - shim/sde.c + disptest.c probe-first method (read-only MDSS dumps,
    test-pattern buffer, timed restore) — highest value, lowest risk.
    Adapt: picasso is video mode, so INTF autorefresh/TE assumptions do
    NOT apply; probe what ABL/UEFI leaves running.
  - xnu/patches/0948 `p mdp dump|smmu|test|scale|restore` poke structure
    (MDSS base 0x0ae00000 same on both; expected DSC/INTF values differ).
  - xnu/patches/0904 A620 GPU — nearly verbatim (same regs/firmware);
    diff QFPROM offsets + zap PAS region first, do not blind-copy.
  - xnu/patches/0905 GPU present+flip logic (VIG SRC0_ADDR + CTL flush);
    recompute geometry for 1080x2400 video, not 2x540 cmd slices.
  - xnu/patches/0906 brightness via DCS 0x51 over DSI0 command DMA —
    mechanism copyable, but picasso backlight is WLED not DCS, so use
    only as a command-DMA pattern, not for brightness.
  - docs/redfin/scanout-plan.md staged experiments a-f (read-only dump ->
    test pattern -> scale -> slot allocator -> per-SwapEnd direct scanout);
    recompute every number: QSEED 1125x2436 -> 1080x2400, carve-out slots
    depend on picasso splash/GOP PA — NOT established.
  - xnu/patches/0943/0944/0946/0947 modes/Hz/TE/CPU-present concepts.
    0998 VRR is a DEAD lead (iOS 16.7 ignores the keys); do not copy.
  - devices/redfin/soc.args DT-provenance convention; device.conf status
    convention ("only what you have seen on the phone is works").
  - phone/gpu + metal2 + turnip: long-term only, after scanout+present
    are solid.
- ios-flame (Pixel 4, SM8150): origin of the SweetFramebuffer/GPU stack;
  NT37280 cmd-mode DSC panel, ABL FB 0x9c400000, INTF1 autorefresh.
- ios-sweet (Redmi Note 10 Pro, SM7150): video-mode panel 1080x2400 —
  closest panel-mode analogue to picasso.
- redwood (POCO X5 Pro, SM7325, upstream): homescreen works, GPU works
  (OpenGL ES via Mesa), Metal partly works; window server draws via
  hakios-metal ~100-118 fps on swipes. Picasso's hakios.adreno.c was
  adapted from this lineage (QFUSE offsets changed for A620).
- HakiOS guide order: describe phone -> bare kernel to launchd -> drivers
  one at a time (CPUFREQ/THERMAL/SOCINFO -> buttons/touch -> UFS -> ADRENO
  (gpu=1, needs fw-gpu/) -> ADSP/audio -> Wi-Fi -> rest); display starts
  as "UEFI's framebuffer, through the loader (to be checked)".

## 6. Phased plan

### Phase 0 — Prove the panel path (diagnostic, no backboardd)
Goal: answer "is the DSI video stream alive after UEFI handoff, and is the
backlight on at early boot?" before investing in backlight/compositor work.
Files: temporary test code in MiniIOMobileFramebuffer start (patch 0001
area) — see §7. No other files.
Verify: user photograph of the physical screen during early boot (ground
truth) + dumpfb capture via tools/usb_screenshot.py (proves the fill landed
in FB memory; the photo proves the panel shows it).
Risks: backlight may already be off by the time the fill runs (then a black
screen is ambiguous — check backlight state separately); the fill may be
overwritten quickly by backboardd (observe during the early-boot window).

### Phase 1 — Backlight / unblank gate (the #1 blocker)
Goal: backlight factor 0 -> 1 and Blanked NO after SpringBoard check-in, so
anything drawn is visible.
Files: userland/src/inject.c (brightness_fixes) + inject_sb.c (patch 0004);
possibly a kernel-side WLED force (PM8150L SPMI) if the userland path
cannot be hooked.
Work: hook the REAL setter path (BKDisplayBrightnessController) instead of /
in addition to BrightnessSystemClient setProperty:forKey:; call
-[SBBacklightController turnOnScreenFullyWithBacklightSource:] after
SpringBoard launch; verify the hook fires (log hit). Alternative: force
WLED on from the kernel (brightness 0xB6C/2924 via SPMI) as a diagnostic to
separate "backlight" from "compositor".
Verify (all required): log shows factor 1.0 + "Set Blanked: NO" (hook
forcing 0 logs a hit); user photograph shows a lit screen (even if only
the step-22 bands or a solid color); touch events no longer cancelled for
backlight dim/off.
Risks: BKDisplayBrightnessController internals differ on iOS 16.5 vs the
version the hooks were written against; forcing WLED from the kernel
bypasses CoreBrightness state and may fight it later.

### Phase 2 — Framebuffer present / swap loop
Goal: continuous frame presentation (CA swap loop) at panel rate.
Files: patches/0001 (MiniIOMobileFramebuffer present path, vsync/presenter
thread, fb_hz/fb_beam/fb_threads); boot args fb_hz=120.
Work: decide FrameInfo (selector 74) policy — keep 0 (vsync-only swaps)
until the loop is stable; vsync source for video mode must be derived
(INTF frame counter vs DSI TE — redfin's TE/autorefresh phasing does NOT
apply); fit 1125x2436 canvas to 1080x2400 panel (current stretch) and
confirm geometry.
Verify: two usb_screenshot captures seconds apart differ (frames
advancing); log shows steady vsync/present cadence; no frame-queue growth.
Risks: wrong vsync source -> tearing or stalls; enabling FrameInfo too
early -> unbounded queue growth (known failure mode).

### Phase 3 — GPU firmware + GMU + step 22 on screen
Goal: Adreno 620 fully up through step 22 with the bands VISIBLE on the
panel.
Files: drivers/gpu/hakios.adreno.c + hakios.gpu.cpp (patch 0003);
pull-firmware.sh / fw-gpu/ staging; configs/picasso.config.
Work: fix fw-gpu staging on a clean payload (broken/empty symlinks); boot
isolated GUI cmdline (adreno=22 gpu=1 adreno_keep=1); confirm steps 0->22
pass, SweetGPU registers, Mesa FD620 context in backboardd.
Verify: log `adreno: 22:` steps pass + `HakiosGPU: SweetGPU service
registered for Mesa/freedreno` + FD620 GLES 3.2 context + user photograph
of the 3 color bands on the physical screen.
Risks: GMU/HFI behaviour may differ per chip (sunfish caution); zap PAS
region / QFPROM offsets must match picasso DT (pil_gpu_region 0x8bc15400);
firmware files missing -> GPU never starts by itself.

### Phase 4 — Hardware-accelerated compositor (homescreen)
Goal: SpringBoard homescreen visible and updating, rendered by the GPU
(not software).
Files: userland build (GL=1 Mesa FD620), inject.c/inject_sb.c,
rc.jobs.picasso (touch automation — blocked on Novatek fix, separate work).
Work: full GUI boot (tools/boot_wifi_gui_test.py family, with Wi-Fi flags
as needed); confirm CA renders via FD620; fix any remaining blanked /
brightness gating from Phase 1.
Verify: user photograph of the homescreen + log (SpringBoard check-in +
CAWindowServerDisplay + FD620 context + steady swaps). Phase gate: no
photo, no "works".
Risks: touch still broken (Novatek SPI rc=-5) blocks interaction even if
the screen renders — display and touch are separate work items; iOS 16.5
selector differences vs redfin's 16.7 patches.

### Phase 5 — Metal / Turnip (deferred)
Goal: Metal path (hakios-metal) and/or Turnip/Vulkan.
Work: only after Phase 4 is solid. hakios-metal chip-ID gate must accept
Adreno 620 (0x06020000) — new chip table via Mesa freedreno_devices.py;
Turnip needs the metal2vulkan/turnip stack ported (redfin-proven, not wired
for picasso).
Verify: Metal test app renders; or Turnip vkcube-equivalent.
Risks: large scope; licensing (Turnip/Mesa MIT, metal2vulkan GPL/LGPL); do
not start before Phase 4.

## 7. FIRST TESTABLE STEP (Phase 0 — do this first)

One change, one boot, one binary answer.
Change: add a boot-arg-gated early framebuffer fill in
MiniIOMobileFramebuffer start (patch 0001 area,
xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp — after vinfo validation,
before backboardd can draw): if boot-arg `fb_test=1`, fill the 1080x2400x32bpp
GOP framebuffer with a solid unmistakable color (magenta 0xFFFF00FF; or reuse
the step-22 orange/white/blue bands) and log `fb_test: filled <w>x<h> with
<color>`. Keep it behind the boot-arg so normal boots are unaffected. Export
via ./build.sh export-patches (patch 0001); build the kernel.
Boot (RAM-boot only): isolated GUI cmdline with the fill enabled, e.g.
`mfb=1 fb_test=1 wifi=0 modem=0 adsp=0 sound=0 mic=0 bt=0 adreno=0 gpu=0
ca_vbl=0` (GPU off — this test is about the panel path only; keep everything
else off to isolate). Watch the physical screen during the first ~60 s after
the kernel starts.
Success looks like: the user sees a solid magenta screen (photograph as
evidence) AND dumpfb captures the same color — DSI video stream + panel +
early backlight are all alive; the blocker is then purely the
backlight/compositor gating (Phase 1).
Failure looks like: screen stays black AND dumpfb shows the fill in memory
-> panel/backlight problem (backlight off or stream dead) — next step is the
MDSS probe (redfin sde.c-style read-only dump of CTL/VIG/LM/DSC/INTF at MDSS
0x0ae00000) to see what UEFI left running. If dumpfb does NOT show the fill,
the FB address/stride is wrong — fix the fill, not the panel.
Notes: do NOT touch MDSS/DSI registers in this test; DisplayReEnablerDxe
exists to keep the ABL display state alive across ExitBootServices. Do not
claim success from the log alone — the photo is the evidence.

## 8. Open questions (need user input or a phone test)

1. Panel variant on the live phone: DTB cmdline selects g7a_37_02_0a —
   confirm the physical panel is the 0a variant (hardware revision M2001G7AE
   vs G7AC if known; the 0b fallback has identical timings, so low-risk).
2. GOP/splash framebuffer physical address on picasso: NOT established
   (redfin's 0xa0000000 does not apply; the 0x9c000000 figure belongs to
   sweet, not picasso). Needed for direct scanout later — read from the
   live boot, do not assume.
3. fw-gpu staging: are a650_gmu.bin / a630_sqe.fw / a620_zap.mdt+.b02
   present in /root/hakios-picasso/fw-gpu/ on k60 right now? (Clean-payload
   symlinks were broken/empty per note.md.)
4. Was the step-22 color band output EVER seen on the physical panel? (Log
   says drawn to FB; no photo/observation recorded.)
5. User help needed for every screen test: photograph the physical screen
   and confirm what is visible — log-only evidence is not accepted for
   display.
6. Touch (Novatek NT36672C, SPI rc=-5) is broken and blocks interaction
   after the screen renders — is touch in scope for this team, or a
   separate track?
7. iOS userland is 16.5 (20F66) while redfin's framebuffer patches target
   16.7.16 — expect selector-level re-checks when adapting 0943-0998.

## 9. Sources

- Track reports: docs/architecture, current-state audit, panel/DSI, GPU,
  references (2026-11-07, research-only).
- Repo files: AGENT.md, README.md, device.conf, configs/picasso.config,
  soc.args, note.md (§2-§3), patches/0001/0003/0004/0006,
  pull-firmware.sh, tools/boot_gui_full.py, tools/boot_wifi_gui_test.py,
  tools/usb_screenshot.py, prebuilt/device-tree.dtb,
  uefi/.../picasso.dsc, .agents/skills/hakios-picasso-porting/SKILL.md.
- Upstream: oosexgang/hakios-xnu-kernel docs (porting/index.md,
  reference/metal.md, reference/drivers.md); ios-redfin origin/ws-n-wifi
  (devices/redfin/device.conf, docs/redfin/scanout-plan.md, port-plan.md,
  xnu/patches/0904/0905/0906/0943-0998, shim/sde.c,
  docs/sunfish/gpu-notes.md).

## 9. Phase 0 execution log (2026-11-08 ~03:11-03:20)

fb_test fill IMPLEMENTED in
build/hakios-xnu-kernel/xnu-src/xnu-8796.121.2/iokit/Kernel/MiniIOMobileFramebuffer.cpp
(IOMobileFramebuffer::start, right after fRowBytes is set): if boot-arg
fb_test=1, fill the GOP FB row-by-row (fRowBytes stride) with magenta
0xFFFF00FF and log "fb_test: filled <w>x<h> with magenta 0xFFFF00FF at
%p (rowbytes %u)". Kernel built 03:11 (BUILD-EXIT:0, C++
MiniIOMobileFramebuffer.cpo, hakios-kernel 15672168 B). Booted 03:17 with
mfb=1 fb_test=1 wifi=0 modem=0 adsp=0 sound=0 mic=0 bt=0 adreno=0 gpu=0
ca_vbl=0. Log confirms: vinfo 1080x2400 depth 32, base 0xfffffff14ec80000,
"fb_test: filled 1080x2400 with magenta 0xFFFF00FF at 0xfffffff14ec80000
(rowbytes 4320)", disp0 registerService() done. Phone alive.
FINDING: dumpfb is NOT implemented in the kernel — tools/usb_screenshot.py
sends "dumpfb" and waits for marker ---BEGIN_SCREENSHOT_BMP---, but no
kernel console handler produces it (only mention of dumpfb in the FB driver
is the fb_test comment). usb_screenshot.py fails with "Marker not found!".
So dumpfb verification is UNAVAILABLE until a dumpfb console command is
added; the PHOTO is the only Phase 0 evidence for now.
WORKFLOW LESSON (xnu edits): edit the file in
build/hakios-xnu-kernel/xnu-src/xnu-8796.121.2/ (the source the build
compiles), NOT in xnu/files/ (regenerated by xnu/export.sh — an edit there
is WIPED by export). Save via ./xnu/export.sh run from the worktree (it
regenerates xnu/hakios.patch + xnu/files/ from xnu-src/). Do NOT run the
full ./build.sh export-patches: it regenerates patches/0001..0006 from
BASE_SHA=HEAD and EMPTIES committed patches 0002 (picasso_usb.c, 3167
lines) and 0005 — restore with git checkout HEAD -- patches/0002... 0005...
if that happens. The build syncs xnu/files/ into xnu-src/ before compiling.

### dumpfb implementation plan (if the screen is black and memory-vs-panel must be distinguished)

Console commands live in ud_process_command() in
build/hakios-xnu-kernel/xnu-src/xnu-8796.121.2/bsd/dev/picasso_usb.c
(line 2341; commands: help | status | panic-test | reset-test | hang-test |
reboot-fastboot). It runs with poll_lock held. To add dumpfb: add a branch
that streams the framebuffer as a BMP (marker ---BEGIN_SCREENSHOT_BMP---,
then 10,368,054 BMP bytes for 1080x2400x32bpp, as tools/usb_screenshot.py
expects). The FB lives in MiniIOMobileFramebuffer (fBase/fPanelWidth/
fPanelHeight/fRowBytes are instance members) — expose a global accessor
from the FB driver (or a registered callback) for picasso_usb.c to call.
Note: streaming 10 MB over the CDC console is slow; pace it (the script
already paces reads). Only worth doing if Phase 0's screen is black.

### Phase 0 RESULT — SUCCESS (2026-11-08 ~03:20, user photo)

User photo (workspace/user/files/1000005216_0_roof.jpg) shows the physical
screen displaying the kernel console text ON a magenta background. The
magenta fill is visible around the console text. **Panel + DSI video stream
+ early backlight are ALL ALIVE.** Phase 0 is a SUCCESS — the blocker is
NOT the panel or backlight hardware. Photo also readable in the log:
"fb_test: filled 1080x2400 with magenta 0xFFFF00FF at 0xfffffff14ec80000
(rowbytes 4320)", "display 1125x2436 on a panel of 1080x2400", "IOMFBDisplayRefresh:
60 Hz", "shadow diff copy buffer ready (10368000 bytes)", disp0 registered.
Notable: the console (printf) draws text into the FB on top of the fill —
the FB is being scanned out live. Also visible: novatek touch identify
fails on all modes (rc=-5, id all zeros) — touch still broken, separate
issue. Next: Phase 1/2 — get backboardd/GUI drawing instead of console
text, verify backlight control, then GPU (Phase 3-4).

### GPU step-22 test — BLOCKED at step 12, missing zap firmware (2026-11-08 ~03:35)

Booted with adreno=22 gpu=1 (base CMDLINE), wifi=0. Adreno steps: step 11
(TrustZone zap query) DONE ("supported GPU 0/0", "TrustZone lists none");
step 12 FAILED: "no fw/a620_zap.mdt in the small image: put the phone's
/vendor/firmware/a620_zap.mdt into fw-gpu/ and run userland/build.sh".
Steps 13-22 NOT attempted ("not kept: the steps did not get as far as a 3D
core that takes commands"). Phone alive. ALSO: the earlier "alive 16-20 s
after step 11" lines were the step-11 TrustZone wait (it completed).
fw-gpu/ does NOT exist on k60; no a620_zap*/a650_gmu*/a630_sqe* anywhere
on the host (searched /root). pull-firmware.sh needs the phone on adb in
Android (not the current state). So the GPU firmware must come from either
(a) the phone's vendor partition (UFS, while on HakiOS — complex), or
(b) a stock picasso fastboot ROM download (vendor.img -> /vendor/firmware).
Next: obtain a620_zap.mdt + a620_zap.b02 (+ a650_gmu.bin, a630_sqe.fw),
place in fw-gpu/, run userland/build.sh, rebuild, re-boot adreno=22.

### GPU firmware staging — how it works (verified 2026-11-08 ~03:37)

The adreno driver is bsd/dev/hakios.adreno.c (in xnu-src, NOT
drivers/gpu/). It loads firmware from the "small image" (ramdisk /dev/md)
via hakios_image_file(directory="fw", name, ...). userland/build.sh
(build/hakios-xnu-kernel/userland/build.sh, lines 428-433) copies
../fw-gpu/* into root/fw/ when ../fw-gpu exists. ../fw-gpu relative to
userland/ = build/hakios-xnu-kernel/fw-gpu/ (CREATED empty 03:37, ready).
Needed: a620_zap.mdt + a620_zap.b02 (step 12), a650_gmu.bin (step 14),
a630_sqe.fw (step 20, SQE). After placing them: run userland/build.sh to
rebuild root.apfs (the ramdisk — the firmware lives there, NOT in the
kernel, so no kernel rebuild needed), then boot with adreno=22 gpu=1
(base CMDLINE) + wifi=0. TheMuppets vendor repo is DMCA-blocked; the
files must come from the phone's /vendor/firmware (Droidian boot + SSH
pull is fastest) or a stock ROM's vendor.img.

### GUI launchd stall — devfs lead (2026-11-08 ~03:40)

gui1 boot (03:27) stuck at "load_init_program: attempting to load
/sbin/launchd" 5+ min, no SpringBoard/backboardd. One concrete error in
the log: "devfs_kernel_mount: kernel_mount failed: 2" (ENOENT). Source:
bsd/kern/bsd_init.c:993-996 mounts devfs at "/dev" during BSD init;
bsd/miscfs/devfs/devfs_vfsops.c:488 kernel_mount("devfs", ..., "/dev").
ENOENT means the /dev directory does not exist in the rootfs (ramdisk
root.apfs) at mount time, or the root was not switched properly. launchd
needs /dev (console, null, etc.), so this is a plausible root cause for
the stall — but NOT yet proven: the 2026-11-04/11-06 GUI boots reached
SpringBoard check-in / root shell, so compare whether devfs failed then
too (check an archived log) before treating it as the blocker. If /dev
is missing from root.apfs, the fix is in userland/build.sh (create
root/dev in the ramdisk) or in the imageboot/root-switch path.

Confirmed: userland/root/ has NO dev/ directory, and userland/build.sh
never creates root/dev (it creates root/lib/*, root/var/root/.ssh,
root/fw but not root/dev). So the ramdisk (root.apfs) lacks /dev. OPEN
QUESTION: is the root filesystem the ramdisk (root.apfs) or system.apfs
(the comment in build.sh calls system.apfs "the root filesystem, the
resident RAM disk, /dev/md0")? If system.apfs is the root, /dev must be
there instead — check whether system.apfs contains /dev before changing
anything. Also compare with the 2026-11-04/11-06 boots (SpringBoard
check-in / root shell) to see if devfs mounted then.

### GUI gui2 boot — devfs FIXED, launchd starts, init stalls after vnodes (2026-11-08 ~03:50)

Fix: added root/dev to userland/build.sh line 510 mkdir (and mkdir -p
root/dev + rebuilt root.apfs via mkapfs.py; now 13 directories, was 12).
Root is md0 (ramdisk root.apfs, boot-arg rd=md0). gui2 boot (wifi=0
adreno=0 gpu=0): "[ ok ] devfs is mounted on /dev" — devfs MOUNTS now;
"=== init: process 1, started as /sbin/launchd ==="; /dev populated
(ufs_uefivarstore, rufs_uefivarstore); "init: up to 32768 vnodes".
Then init goes QUIET — no SpringBoard/backboardd, no job output after
~4 min (log ~900 lines, last init line is the vnodes one). So the /dev
fix unblocked launchd, but init stalls (or runs silently) after vnodes.
Next: find why init does not load /LaunchDaemons jobs / start
SpringBoard — check whether init expects a job, a Mach service, or the
console shell; compare with the 2026-11-06 boot that got a root shell.
(Note: the first gui2 boot attempt never started — killing the hung SSH
session killed the setsid boot script before it ran; use nohup + disown
and verify build/gui2-run.log exists before trusting a boot.)

### GPU step-22 — SUCCESS (2026-11-08 ~03:57)

Firmware was ALREADY on id-it-tritm-2: /home/tritm/hakios-u/fw-gpu/
(a620_zap.mdt/.b00/.b01/.b02/.elf, a650_gmu.bin, a630_sqe.fw) — copied
to k60 via rsync from tritm@100.67.179.75 (scp brace expansion does NOT
work; use rsync -av). md5: a620_zap.mdt a112f200..., a620_zap.b02
0d02acbd..., a650_gmu.bin a2b9504e... (41548 B), a630_sqe.fw 42b0a156...
(32304 B). Copied into userland/root/fw/ + rebuilt root.apfs (13 files,
14 dirs, 45 blocks). Boot adreno=22 gpu=1 wifi=0: step 12 zap loaded,
steps 13-21 passed, STEP 22 DONE — "3D core rendered self-test pattern
successfully in memory (screen display suppressed)": first pixel
00 80 ff ff (blue), middle ff ff ff ff (white), last row ff 80 00 ff
(orange); ring read up to 123, status 0x1; cache words 0xc0c0c0c,
0xd0e0d0e as told. "THE GPU STAYS ON for the system to draw with: the
service HakiosGPU". Sensors 44.5-45.1 C, clock 275 MHz. Adreno 620 GPU
is FULLY UP (steps 1-22). The pattern was rendered in memory, not shown
on screen (suppressed) — next is Phase 3/4: GPU compositor / display
the rendered frame on the panel.

### GUI "stall" CLARIFIED — init is NOT stuck, it ends at a root shell (2026-11-08 ~04:05)

The gui1/gui2 "stall" was a misread. The ramdisk-root boot (rd=md0)
runs the HakiOS init (userland/src/init.c) which does its self-tests
then drops to an interactive root shell — BY DESIGN. Proof (gpu22 log):
"=== 22 checks, 3 failed ===" then the banner "Darwin 22.5.0 Darwin
Kernel (arm64) - HakiOS Picasso (SM7250) / Root Console (USB CDC-ACM
Serial) - uid: 0 (root)" and the prompt "root@hakios-picasso:/#"
(the boot script then typed "wifi status"). The 3 failed checks:
mount a memory filesystem on /private/var, create the directories of
the data volume, follow a symlink to a directory — non-fatal (check()
only counts, never aborts); /etc/rc does not exist, run_rc() returns
silently. So this boot mode NEVER starts SpringBoard — init does not
exec launchd.darwin here. SpringBoard needs the FLASH-ROOT mode:
boot-arg root_apfs=ufs_darwin (build.sh:505,527) makes the Darwin
volume on the phone's flash the root; init mounts it on /flash, makes
it the root, moves the ramdisk, and execs Apple's /sbin/launchd (init.c
header lines 10-11, 1351). The 2026-11-04 SpringBoard check-in must
have used that mode. Next GUI step: boot with root_apfs=ufs_darwin and
watch for SpringBoard/backboardd. (Note: the /dev fix was still real
and necessary — without it devfs failed and launchd never started.)

### Flash-root boot (root_apfs=ufs_darwin) — Darwin volume mounts, still ends at shell (2026-11-08 ~04:16)

Booted with HAKIOS_EXTRA_ARGS='wifi=0 root_apfs=ufs_darwin'. Log: "-- the
Darwin root volume on /dev/ufs_darwin as the root --", "[ ok ] mount the
Darwin volume read only" (flash_root mounts /dev/ufs_darwin at /flash
MNT_RDONLY — read-only, no UFS write), cryptexes OS/App mounted, 27 ok /
6 FAIL in checks. BUT init STILL ends at the interactive root shell
("root@hakios-picasso:/#") — no SpringBoard/backboardd. Root cause: in
the current init.c, process 1 NEVER execs launchd.darwin; the PID-1
flow is flash_root -> show_system -> mount_writable -> Dropbear ->
self-tests -> configure_vnodes -> run_rc -> interactive_shell (for(;;)
loop) -> halt (unreachable). The header comment's "exec
/sbin/launchd.darwin" only applies inside the job runner (init.c:1351):
when a launchd JOB runs /sbin/launchd.darwin it is replaced with
Apple's /sbin/launchd in flash_root_mode. But /LaunchDaemons has only
com.hakios.varstore.plist — NO job starts launchd or SpringBoard. So
SpringBoard needs to be started explicitly: either a new LaunchDaemon
job, or a manual command from the interactive shell (the shell's
run_command can exec any binary; 'unlock' assumes SpringBoard already
runs). The 2026-11-04 SpringBoard check-in must have used a job or a
manual start that no longer exists. Next: try starting Darwin's
launchd/SpringBoard from the shell via the USB console, or add a
LaunchDaemon job for it.

### Flash-root shell exploration — SpringBoard/backboardd binaries NOT in base volume; cryptex ls CRASHES phone (2026-11-08 ~04:20)

From the interactive shell (flash-root boot): /System/Library/CoreServices/
SpringBoard.app/ EXISTS (resources: .loctable, .strings, Assets.car, etc.)
but the SpringBoard BINARY is absent, and /usr/libexec/backboardd does
not exist — iOS 16 binaries live in the OS cryptex, not the base volume.
The HakiOS shell is a simple runner (no ';' or pipes — a compound line
is rejected as one command name). Sending `ls /private/preboot/Cryptexes/OS`
CRASHED the phone (serial "device disconnected", log stopped 04:16:40,
phone back in xnuloader fastboot). Do NOT ls the cryptex path again —
accessing it panics the kernel (cryptex mount incomplete or path
invalid). SpringBoard needs Darwin launchd + backboardd + cryptex
binaries — a bigger undertaking than the GPU/display work; the
cryptex path is currently a crash risk, not a working route.

### Phase 3 — display the step-22 pattern on screen (2026-11-08 ~04:27)

The "screen display suppressed" was just a commented-out call: adreno.c
line 3160 had `// MiniIOMobileFramebufferShow(...)`. The function exists
(MiniIOMobileFramebuffer.cpp, copies row-by-row handling the 4352 source
stride vs 4320 display stride) and is used by hakios.gpu.cpp. Uncommented
it, saved via ./xnu/export.sh, rebuilt kernel (BUILD-EXIT:0, 04:23, 0
errors), booted adreno=22 gpu=1 wifi=0. NOTE: the log still prints
"(screen display suppressed)" — that is the HARDCODED printf string
next to the call, not a status; the Show call is in the built kernel
(verified in xnu-src line 3160, kernel compiled hakios.adreno). The
pattern should now be ON the physical screen. NEED: user photograph to
confirm the 3 color bands (blue/white/orange) are visible. If the screen
still shows console text only, the framebuffer may be overwritten by
later console output or the Show ran before the panel was ready.

### SpringBoard job mechanism (research, no phone change) (2026-11-08 ~04:30)

LaunchDaemon jobs are GENERATED by userland/mkjobs.py from the Darwin
volume listing (build.sh:426): `./mkjobs.py $VOLUME $DARWIN/rootfs
root/LaunchDaemons $DARWIN_JOBS`. Default DARWIN_JOBS (build.sh:101):
notifyd, cfprefsd, logd, wifid, etc. INJECT (build.sh:130) lists programs
started with libhakios.dylib — including backboardd, containermanagerd.
mkjobs.py --rest adds the other Darwin jobs + LaunchAngels (wallpaper,
keyboard, now playing) on demand via Mach services, excluding jobs
without Mach services, jobs whose program is absent at runtime, and DENY
(hardware not present). BUT: the HakiOS init (PID 1) does NOT run
Darwin's launchd — `ps` in the flash-root boot shows only PID 1 (init)
and ps. So no LaunchDaemon job runs at all in the current boot, and
nothing starts backboardd/SpringBoard. Starting SpringBoard requires
starting Darwin's launchd itself (as PID 1 via exec, or as a child) —
a separate, larger piece of work, and the cryptex interaction is
fragile (ls of the cryptex path crashed the phone 04:20). Do not pursue
without a deliberate plan.

### Phase 3 VERIFIED — 3 color bands ON the physical screen (user photo 2026-11-08 06:26:13)

User photo (1000005218_1_tahu.jpg, 06:26:13): the screen shows 3 color
bands AROUND the console text. The photo is rotated 90 deg (phone on
its side); the bands read left-to-right in the photo as pink/orange,
white, blue — which on the portrait phone are horizontal bands
top=orange, middle=white, bottom=blue. This MATCHES the step-22 pixel
reads exactly: first pixel 00 80 ff ff (BGRA: orange), middle
ff ff ff ff (white), last row ff 80 00 ff (BGRA: blue). The GPU-rendered
pattern IS on the physical screen, with the kernel console text drawn
over it. Phase 3 COMPLETE: adreno 22 steps pass + HakiosGPU service +
GPU pattern visible on screen (photo evidence). Remaining Phase 3 item:
FD620 GLES 3.2 context in backboardd (needs Darwin launchd running).
Next: Phase 4 (SpringBoard homescreen) — needs Darwin's launchd started;
cryptex path is a crash risk, needs a deliberate plan.

### Phase 4 attempt — exec Darwin launchd via /etc/rc: FAILED, launchd exits, system halts (2026-11-08 ~06:28-06:33)

Found the designed handover: init.c header says process 1 runs etc/rc,
"usually just `exec /sbin/launchd.darwin`, which turns process 1 into
the Darwin launchd". build.sh:411 copies ${RC:-rc.launchd} to
root/etc/rc; rc.launchd exists in userland/ ("# Experiment: hand process
1 over to the real Darwin launchd. / exec /sbin/launchd.darwin") but was
NEVER in root/etc/ (only motd) because the full build.sh was not run —
only minimal mkapfs rebuilds. Created root/etc/rc from rc.launchd,
rebuilt root.apfs (14 files), booted flash-root (root_apfs=ufs_darwin).
Log: "$ exec /sbin/launchd.darwin" RAN, then "stage0: cannot mount
/dev/md1 on /hakios: No such file or directory", "Kext loading now
disabled", "syncing disks... Killing all processes", vfs_unmountall,
"IOServicePH::systemHalt", "continuing". Darwin's launchd EXITED
immediately (no SpringBoard/backboardd output, no error line from
launchd itself), PID 1 exiting triggered the halt; the kernel stayed
alive in its idle loop (adreno sensors 46.8C) with NO userland. The
kernel console `reboot-fastboot` still worked (phone back in fastboot).
REVERTED: root/etc/rc removed, root.apfs rebuilt (13 files) — next boot
returns to the interactive shell. CONCLUSION: the simple exec handover
does not work — Darwin's launchd needs more than being exec'd as PID 1
(missing md1 mount? missing Mach bootstrap? cryptex?). Do NOT retry the
bare exec; the next attempt needs to find WHY launchd exits (its stderr
goes nowhere by default — build.sh QUIET=1 note; try without QUIET, or
run launchd from the shell as a child first to see its error).

### Phase 4 — running Apple launchd as a child ALSO panics (PID 1 dies) (2026-11-08 ~06:41)

After the /etc/rc exec failure, tried running Apple's /sbin/launchd
from the interactive shell (flash-root boot) as a child process to see
its error. Result: KERNEL PANIC. Log: "coredump (init, pid 1): failed
to open core dump file /private/var/cores/init.core: error 2",
"Failed to generate core file for pid: 1", "panic(cpu 5 caller
0xfffffff007818518):", "Debugger message: panic", "Panicked task
0xfffffff3477026d0: 11 pages, 1 threads: pid 1: init", "CORE 5 is the
one that panicked". The panic reason is EMPTY (just "panic") — PID 1
(the HakiOS init) itself DIED when launchd was fork/exec'd, and the
kernel panics when PID 1 dies. Phone back in xnuloader fastboot.
CONCLUSION: Apple's launchd cannot run here at all — neither as PID 1
(via /etc/rc: exits immediately, system halts) nor as a child (kills
PID 1, kernel panic). The Darwin launchd path is fundamentally blocked
in this environment; SpringBoard needs a different route (e.g. a
HakiOS-native window server/compositor using the working GPU, or
figuring out what launchd needs that the kernel lacks). Do NOT run
/sbin/launchd again without understanding why PID 1 dies.

### Upstream update to 63fc2b4 (2026-11-08 ~07:00-07:30) — launchd STILL exits

User: stay on HakiOS, update from upstream, follow their direction.
Upstream remotes: hakios-xnu-kernel oosexgang (pinned 96ffbc5, remote HEAD
63fc2b4, 261 commits behind); hakios-uefi oosexgang (687fa6c, already
current); Mu-Silicium YinCQL/uefi_picasso (b363060, already current).
xnu/userland/metal all live in hakios-xnu-kernel. Fetched all.
KEY FINDING: upstream init.c is UNCHANGED 96ffbc5->63fc2b4 — picasso's
init.c (3151 lines) = upstream's 1568 lines + 1587 picasso lines; the
flash_root/exec launchd.darwin mechanism is IDENTICAL. So the launchd
failure is NOT a picasso init.c divergence. Relevant upstream changes:
inject_sb.c +621, HakiosMetalCA.m +444, new rc.jobs (bench.panel/xr,
interactive, layers, m2vtest, vktest), userland/build.sh +68.
Update: backed up worktree diff to /root/picasso-wt-96ffbc5-backup.patch
(406KB) + untracked picasso_usb.c/fw-gpu; stash -> checkout 63fc2b4 ->
stash pop; 6 conflicts (5 config/doc union-merged; framebuffer took
upstream side both blocks: unconditional drawLayer + gShownSurface, new
stats printf + share-stats). Verified fb_test intact, Show uncomment
(adreno.c:3160) intact, /dev fix intact. build.sh pin 96ffbc5->63fc2b4;
upstream/ checkout moved to 63fc2b4. Kernel build BUILD-EXIT:0 (0 errors,
15674144 B, 07:17); userland rebuilt (root.apfs 07:18, root/dev + fw-gpu
present). NOTE: the full userland build did NOT copy rc.launchd to
root/etc/rc (root/etc/ had only motd) — copied it manually, rebuilt
root.apfs (07:25).
BOOT TEST (new upstream, flash-root, /etc/rc present): SAME FAILURE.
"$ exec /sbin/launchd.darwin" ran, then "stage0: cannot mount /dev/md1
on /hakios: No such file or directory", "Kext loading now disabled",
vfs_unmountall, systemHalt. Launchd exits immediately; phone in halted
idle state; reboot-fastboot recovered it. CONCLUSION: the upstream update
does NOT fix the launchd handover — the cause is NOT init.c or the
userland code that changed. Next leads: (a) boot-args — picasso CMDLINE
lacks ios-sweet's launchd_unsecure_cache=1, wdt=-1,
unentitled_ios_sim_launch=1, rootrw=1 (but upstream redwood doesn't use
those either); (b) trust cache / AMFI / code signing of launchd and
daemons; (c) cryptex; (d) how redwood actually boots launchd (its
boot-args/device config).

### Boot-args diagnostic (ios-sweet args) — launchd STILL exits (2026-11-08 ~07:30)

After the upstream update, tried ios-sweet's launchd boot-args as a
diagnostic (no rebuild, boot-args only): launchd_unsecure_cache=1,
wdt=-1, unentitled_ios_sim_launch=1, rootrw=1, on top of flash-root +
/etc/rc. SAME RESULT: "$ exec /sbin/launchd.darwin" ran, then
vfs_unmountall + systemHalt, no SpringBoard/backboardd, no launchd
output. The trust-cache boot-args do NOT fix it. (Note: upstream
redwood does not use these args either.) Phone rebooted to fastboot.
Remaining leads for why launchd exits silently (no stderr output,
/etc/quiet does not exist so stderr is on the console): (a) launchd
needs a file/service that is missing (e.g. /var/db, a Mach service,
the cryptex mount); (b) the Darwin volume's launchd expects a
different environment (it is from a different iOS version than the
kernel expects); (c) the kernel's stage0/md1 mount failure
("cannot mount /dev/md1 on /hakios") is a symptom of the halt path,
not the cause. The most informative next step is to capture launchd's
own stderr/exit — e.g. run launchd under a wrapper that logs its exit
status, or strace-style tracing, or check what files launchd opens
first (it exits before printing anything, so it fails very early).

### Launchd exit investigation (2026-11-08 ~07:40, read-only, no boot)

- stage0/md1 is a RED HERRING: stage0.c mounts /dev/md1 on /redwood
  then execs init; on failure it prints "stage0: cannot mount /dev/md1
  on /hakios" and halts. That line appears AFTER launchd exits, during
  the halt — the kernel re-runs stage0 as the init program, but md1 is
  already unmounted by vfs_unmountall, so it fails. It is a symptom of
  the halt, not the cause of launchd exiting.
- Kernel load_init_program (kern_exec.c ~7091-7122): with
  launchdsuffix=release it loads /sbin/launchd as PID 1, and panics if
  that exec fails. In HakiOS, /sbin/launchd in the ramdisk is a symlink
  to the HakiOS init, so PID 1 is the HakiOS init, not Apple's launchd.
  Apple's launchd is only reached later, via init's /etc/rc exec.
- Launchd exits SILENTLY (no stderr; /etc/quiet does not exist, so
  stderr is on the console). It is not a crash (no panic) and not an
  execve failure (init would print "execve: ..."). Launchd starts and
  then calls exit() itself, very early. Unresolved: what it needs.
  Candidates: a missing file/service, the Mach bootstrap port setup
  (launchd expects to be the bootstrap server; execve from init may
  not give it the bootstrap state the kernel gives PID 1), or the
  Darwin volume/iOS version. Next diagnostic ideas: capture launchd's
  exit path via a kernel log when PID 1 exits, or inspect the Darwin
  volume's launchd (needs flash access); do NOT run /sbin/launchd from
  the shell (panics PID 1).

### BREAKTHROUGH: launchd calls reboot(RB_HALT) itself (2026-11-08 ~08:10)

User: find a way to capture why launchd exits. Added 3 kernel
diagnostics (PICASSO-DIAG) and rebuilt (kernel md5 99fab30b, 08:06):
(1) exit_with_reason in kern_exit.c — PID 1 exit() path; (2) proc_exit
in kern_exit.c — final reaping; (3) reboot() in kern_xxx.c — reboot
syscall. Booted flash-root + /etc/rc. RESULT: the exit() diagnostic
NEVER fired — launchd does NOT call exit(). Instead:
"PICASSO-DIAG: PID 1 (launchd) called reboot(), opt=0x8" — launchd
itself calls reboot(RB_HALT) (0x8 = RB_HALT). So launchd does not
crash or exit; it DELIBERATELY halts the system on a fatal startup
condition. (proc_exit also fired for PID 1 "init" exitval=0 at log
line 1190, just before the reboot call — likely the exec transition
quirk, not the cause.) Kernel's own proc_handle_critical_exit
"pid 1 exited" printf never appeared because launchd never exits via
exit(). NEXT: find WHAT condition makes launchd call reboot(RB_HALT).
Candidates: it cannot create the Mach bootstrap port, cannot load its
job configuration, or fails an early check. A useful next diagnostic
is whether launchd reached main (kern.initproc_spawned sysctl, set by
launchd itself) — add it to the reboot() diagnostic — and/or trace
what launchd opens/does just before the reboot call.

### initproc_spawned=0 — launchd fails BEFORE main() (2026-11-08 ~08:20)

Added initproc_spawned to the reboot() diagnostic (made it non-static
in kern_exit.c, extern in kern_xxx.c; first build failed on the extern
landing between the `int` return type and `reboot(...)` — fixed by
moving it before `int`; rebuilt ok, kernel md5 aee5c1ce, 08:15).
Booted flash-root + /etc/rc. RESULT:
"PICASSO-DIAG: PID 1 (launchd) called reboot(), opt=0x8,
initproc_spawned=0".
initproc_spawned is set to 1 by launchd itself when it reaches main()
(via sysctl kern.initproc_spawned). Value 0 means launchd calls
reboot(RB_HALT) BEFORE reaching main() — it fails in the earliest
startup stage (dyld load, early init, or a pre-main check). It does
not crash and does not call exit(); it deliberately halts. NEXT: find
what fails before main. Most direct: inspect the Darwin volume's
launchd binary and its libraries from the HakiOS shell (mount
/dev/ufs_darwin, check /sbin/launchd exists, its size, and the
libraries it links against), and/or check whether the Mach bootstrap
port setup launchd expects is present.

### Open trace: launchd opens ONLY /dev/console, then reboots (2026-11-08 ~08:30)

Added a PID-1 open() tracer (open_nocancel in vfs_syscalls.c, logs
path via copyinstr; kernel md5 6994db7d, 08:27). Booted flash-root +
/etc/rc. The opens by PID 1, in order:
  /dev, /dev/zero, /dev/null, /dev/random (init startup),
  /redwood/etc/inject.conf, /redwood/etc/rc (init reading rc),
  then proc_exit PID 1 (init) exitval=0,
  then "PID 1 open: /dev/console" — launchd's FIRST and ONLY open,
  then "PID 1 (launchd) called reboot(), opt=0x8, initproc_spawned=0".
So after exec, launchd opens /dev/console and NOTHING else — no
library, no config, no LaunchDaemon — then calls reboot(RB_HALT)
before main(). It fails immediately after console setup. NEXT: check
whether the /dev/console open succeeds (log its return value/fd) and
what launchd does next (likely the Mach bootstrap port setup).

### Open results: /dev/console succeeds; launchd opens nothing else (2026-11-08 ~08:35)

Open tracer now logs err+fd (kernel md5 dc20e124). Flash-root + /etc/rc:
  /dev, /dev/zero, /dev/null, /dev/random -> err=0 fd=3 (init),
  /redwood/etc/inject.conf -> err=2 ENOENT (init; harmless, init continues),
  /redwood/etc/rc -> err=0 fd=3,
  proc_exit PID 1 (init) exitval=0,
  /dev/console -> err=0 fd=3 (launchd, SUCCESS),
  reboot() opt=0x8, initproc_spawned=0.
So /dev/console opens fine — the failure is NOT a file-open error.
Launchd opens only /dev/console via open(). BUT dyld loads libraries
via openat(), which is NOT traced yet — library opens are invisible
in this trace. NEXT: add the same tracer to openat_nocancel to see
which libraries dyld loads (and which one fails) before the reboot.

### Openat trace: launchd opens NOTHING via openat either (2026-11-08 ~08:45)

Added openat tracer (kernel md5 f6d921d1). Flash-root + /etc/rc.
PID 1 opens (open + openat), in order:
  /redwood/etc/newfile -> err=30 EROFS, /redwood/share/pattern.bin -> 0,
  /dev, /dev/zero, /dev/null, /dev/random -> 0,
  /redwood/etc/inject.conf -> err=2 ENOENT (init, harmless),
  /redwood/etc/rc -> 0,
  proc_exit PID 1 (init) exitval=0,
  /dev/console -> err=0 fd=3 (launchd),
  reboot() opt=0x8, initproc_spawned=0.
NO openat calls at all from launchd. It opens only /dev/console via
open(), then reboots. No library opens are visible — consistent with
dyld loading libraries from the shared cache (mapped, not opened).
So the failure is NOT a missing file. It is in a non-file operation
right after console setup — most likely the Mach bootstrap port setup
(launchd must become the bootstrap server as PID 1) or an early
pre-main check. The HakiOS init execs launchd, so launchd inherits
init's task/ports; if the kernel's bootstrap port is not what launchd
expects, its bootstrap setup fails and it halts. NEXT: trace the Mach
bootstrap (task_get_bootstrap_port / bootstrap_checkin) for PID 1,
or compare how the kernel sets up PID 1's bootstrap port vs what
launchd expects.

### Mach bootstrap — preparation (2026-11-08 ~08:50, read-only)

The failure is in a non-file op after /dev/console setup, most likely
Mach bootstrap. Read-only check: the kernel sets up PID 1's task in
bsd_utaskbootstrap() (bsd_init.c:1170, finds initproc via proc_find(1));
the bootstrap port itself is a Mach-layer concept (osfmk/kern, e.g.
mach_node.c), not set in bsd_init.c. Tracing it means instrumenting
Mach traps (task_get_bootstrap_port / bootstrap_checkin) for PID 1 —
a deeper kernel change than the syscall tracers so far. Awaiting
user's choice between (a) Mach bootstrap tracing and (b) inspecting
the Darwin volume's launchd/iOS version.

### Attempt 2 binary — verified: it WAS Apple's launchd (2026-11-08 ~08:50)

A background review suggested attempt 2 (06:41, /sbin/launchd from the
shell) might have run the HakiOS init symlink instead of Apple's
launchd. Checked init.c: line 40 comment — "the root is the Darwin
volume: its /sbin/launchd is Apple's, there is no launchd.darwin" —
and run_command lines 1351-1352, which in flash_root_mode replace
"/sbin/launchd.darwin" with "/sbin/launchd" because "the root is
Darwin's own volume: its launchd is that". So in the flash-root boot,
/sbin/launchd IS Apple's launchd. Attempt 2 did run Apple's launchd
as a child, and the PID 1 panic conclusion stands. No correction
needed.

## Mach bootstrap trace of PID 1 (2026-11-08 ~08:51-09:11, user-directed)

Method: PICASSO-DIAG tracers in osfmk/kern/ipc_tt.c:
- task_get_special_port_internal() before `*portp = port;`: logs PID 1
  which + returned port + itk_bootstrap value.
- task_set_special_port_internal() before `out:`: logs PID 1 which +
  port + rc + itk_bootstrap value.
Saved via ./xnu/export.sh; kernel md5 f044b04ab22d69fa4956310c08f787ad
(built 08:59, 0 errors, CC ipc_tt.o). Boot: flash-root + /etc/rc,
wifi=0 root_apfs=ufs_darwin. Log build/wifi_scan_gui.log (fresh 09:11).

Task special port numbers (osfmk/mach/task_special_ports.h):
KERNEL_PORT=1, HOST_PORT=2, NAME_PORT=3, BOOTSTRAP_PORT=4,
INSPECT=5, READ=6, ACCESS_PORT=9, DEBUG_CONTROL=10, RESOURCE_NOTIFY=11.

FINDING — launchd's Mach activity before reboot (log lines 1213-1218):
- task_set_special_port(which=2 HOST_PORT, port=0xfffffff74f219760) rc=0
- task_get_special_port(which=9 ACCESS_PORT) -> port=0 (NULL=1)
- open /dev/console -> err=0 fd=3
- reboot(RB_HALT) opt=0x8, initproc_spawned=0
Launchd NEVER calls task_get_special_port(which=4 BOOTSTRAP) and NEVER
calls task_set_special_port(which=4). It never requests or sets the
bootstrap port at all.

FINDING — PID 1 task bootstrap port is NULL:
- itk_bootstrap == 0 (bootstrap=0) on EVERY PID 1 special-port call,
  for BOTH the HakiOS init (lines 781-783) and launchd (1213-1214).
- Root cause of the NULL: new tasks inherit itk_bootstrap from the
  parent (ipc_tt.c:257-258 ipc_port_copy_send_mqueue(parent->itk_bootstrap));
  PID 1's parent is kernel_task, which has no bootstrap port, so PID 1
  inherits NULL. No kernel code allocates a bootstrap port for PID 1
  (searched bsd_init.c, task.c, ipc_tt.c — only assignments are
  IP_NULL init, parent inherit, and task_set_special_port case).
- Stock XNU also leaves PID 1 bootstrap NULL; launchd is expected to
  CREATE its own bootstrap port. Our launchd reboots BEFORE reaching
  that code, so the NULL bootstrap is NOT (by itself) the trigger —
  the failure is in dyld/libSystem init, between /dev/console open
  and bootstrap setup, before main().

CONCLUSION: the Mach bootstrap get/set path is RULED OUT as the
failure point (launchd never uses it). Next candidates: (a) test
providing a bootstrap port for PID 1 (kernel bsd_utaskbootstrap:
ipc_port_alloc + task_set_special_port BOOTSTRAP) to see if launchd
proceeds past libSystem init; (b) broaden trace to mach_port_allocate
/ sysctl / mach_msg for PID 1; (c) inspect Darwin volume launchd /
iOS version. init.c has NO Mach API usage (no mach_task_self,
task_set_bootstrap, mach_port_allocate) — a userspace bootstrap fix
in init.c would need Mach headers/linking added.

## Mach bootstrap trace, part 2 — mach_port_allocate + sysctl (2026-11-08 ~09:17-09:39)

User chose option (a) implicitly (no objection; standing instruction to
keep experimenting). Added tracers:
- osfmk/ipc/mach_port.c mach_port_allocate_full(): logs PID 1 right +
  kr + name (gated on task_pid(current_task())==1).
- bsd/kern/kern_newsysctl.c sysctl(): logs PID 1 numeric OID
  (name0/name1/namelen).
- bsd/kern/kern_newsysctl.c sys_sysctlbyname(): logs PID 1 name string.
Kernel md5 368205ac1a950705ce92370b0441dd48 (09:37, 0 errors).
Patch now has 11 PICASSO-DIAG lines. Boot: flash-root + /etc/rc.
Log build/wifi_scan_gui.log (fresh 09:39).

RESULT — launchd's COMPLETE kernel interaction before reboot
(log lines 1226-1239):
1. sysctlbyname(hw.memsize)
2. sysctlbyname(kern.maxvnodes)
3. task_set_special_port(which=2 HOST_PORT) rc=0
4. task_get_special_port(which=9 ACCESS_PORT) -> NULL
5. open /dev/console -> err=0 fd=3
6. reboot(RB_HALT) opt=0x8, initproc_spawned=0

ZERO mach_port_allocate_full calls from PID 1 (entire log).
ZERO numeric sysctl() calls from PID 1 (entire log).
(The HakiOS init uses sysctlbyname too: kern.version, hw.machine,
hw.memsize, hw.pagesize, hw.ncpu, kern.bootargs x2.)

CONCLUSION: launchd makes exactly 6 kernel interactions, all
succeeding (access-port NULL is normal), then reboots. The failure
involves NO additional syscall or Mach trap — it is a PURE USERSPACE
check in dyld/libSystem init, after /dev/console is opened. Kernel
tracing has reached its limit; the cause must be in userspace:
dyld shared cache validity/version, code signature / AMFI trust,
or a hardware/feature check. Next step is option (c): inspect the
Darwin volume's launchd binary + dyld shared cache + iOS version.

## Direction (b) — bootstrap port for PID 1, BUILD READY (2026-11-08 ~09:57, subagent)

User directed all 3 directions in parallel. (b) implemented + built,
phone NOT booted (direction (c) still using it).

Code: new helper task_setup_bootstrap_port(task_t) in
osfmk/kern/ipc_tt.c (~line 2143): ipc_port_alloc(task->itk_space,
IPC_PORT_INIT_MESSAGE_QUEUE, &nm, &prt) -> ip_mq_unlock(prt) ->
ipc_port_make_send_any(prt) -> task_set_special_port_internal(task,
TASK_BOOTSTRAP_PORT, snd), with PICASSO-DIAG printf on success
("PID 1 bootstrap port set: port=%p name=%d kr=%d") and on failure.
Declared in osfmk/kern/task.h:827. Called from bsd_utaskbootstrap()
in bsd/kern/bsd_init.c:1208, after task_clear_return_wait (IPC already
enabled). Non-fatal on failure (logs, continues).
Why in ipc_tt.c: first attempt inline in bsd_init.c failed to build
(11 errors — struct task opaque in BSD code, ipc_port APIs not
visible; no BSD file uses ipc_port_alloc). Reverted; helper moved to
Mach side where task->itk_space + ipc_port_alloc + make_send_any are
available. Saved via ./xnu/export.sh (16 PICASSO-DIAG lines in patch).

Build: kernel md5 32b266344732d74d9c234b2e8f643c43 (was 368205ac),
BUILD-EXIT:0, 0 errors, size 15674192 B (+48), CC ipc_tt.o +
CC bsd_init.o compiled, timestamp Nov 8 09:56.

Boot test (PENDING, waits for direction (c) to finish with phone):
expect "PICASSO-DIAG: PID 1 bootstrap port set: port=<non-null>
kr=0" early in boot, and bootstrap=<non-null> on later
task_get/set_special_port lines. If launchd proceeds past
/dev/console open or requests TASK_BOOTSTRAP_PORT=4, bootstrap was
the blocker. If it still reboots with a valid bootstrap port,
hypothesis ruled out.

## Direction (c) — Darwin volume inspection, DONE (2026-11-08 ~09:46-09:57, subagent)

Flash-root boot WITHOUT /etc/rc landed at interactive shell. Fresh gui
log 73691 B (mtime Nov 8 09:46), run-log diag-darwin-inspect-run.log
74827 B. Log: "-- the Darwin root volume on /dev/ufs_darwin as the
root --" (line 793), "=== 33 checks, 6 failed ===" (1220), shell
prompt (1233). Shell interactive via pyserial (pwd -> /).
Shell note: its `ls` takes no flags; plain `ls <dir>` prints long
format with sizes; `ls <file>` returns ENOTDIR (err=20) if the file
exists, ENOENT (err=2) if absent. PICASSO-DIAG open tracer (kernel
368205ac) independently confirms the same existence results below.

Inspected (exact):
- /sbin/launchd EXISTS, 34560 B, -rwxr-xr-x
- /sbin/launchd.darwin EXISTS, 525088 B, -rwxr-xr-x (the binary
  /etc/rc execs)
- SystemVersion.plist: iOS 16.5, build 20F66 (ProductName "iPhone
  OS", ProductVersion 16.5, ProductBuildVersion 20F66, BuildID
  FFEC7764-F0B1-11ED-B719-7ADF6D06363D, SystemImageID
  6B192A83-67BB-4A64-844A-C38510C88973)
- uname -a: Darwin hakios-picasso 22.5.0 — kernel and Darwin volume
  versions MATCH (16.5 / Darwin 22.5.0)
- /System/Library/Caches/com.apple.dyld/ ABSENT (ENOENT); /System/
  Library/dyld/ ABSENT; /System/Library/Caches/ ABSENT ENTIRELY;
  /private/var/db/dyld/ ABSENT
- /usr/lib/dyld EXISTS as a file (ENOTDIR — the loader binary)
- /usr/lib/libSystem.dylib ABSENT (ENOENT). Only libSystem.B_asan.
  dylib (73648 B) + symlink libSystem_asan.dylib exist in /usr/lib/
- /usr/lib/system/ contains only an empty introspection/ subdir — no
  libdispatch/libxpc/libsystem_kernel etc. as files
- /System/Library/CoreServices/SpringBoard.app/SpringBoard EXISTS,
  112048 B, -rwxr-xr-x (resources present: Assets.car, .loctable,
  sounds, _CodeSignature)
- /usr/libexec/backboardd EXISTS, 1182960 B, -rwxr-xr-x
- /usr/libexec/launchd_cache_loader EXISTS, 72336 B
- ls /: /Library, /private, /redwood, /dev, /.ba, /.mb, /tmp ->
  private/var/tmp, /Applications, /Developer, /cores, /fw — many
  dirs size 0 (empty)
- /System/Library/: many subdirs present but EMPTY (size 0): e.g.
  BackBoard, CoreServices, Security, TextInput, VideoEncoders

CORRECTION to the 04:18 finding: the SpringBoard binary and
backboardd DO exist (exact sizes above, confirmed by two methods:
directory listing + open err=20 ENOTDIR vs err=2 ENOENT). The 04:18
"binary absent / backboardd does not exist" impression was wrong
(likely a truncated listing). Binaries are NOT the problem.

Interpretation: NO version mismatch; NO missing binary. The notable
anomaly is the dyld shared cache and libSystem being invisible from
this shell — consistent with iOS 16 design (system files live in the
OS cryptex; base volume is sparse), but cryptex content is not fully
visible from this shell. Cryptex-mount completeness could NOT be
directly verified (did NOT ls /private/preboot/Cryptexes/OS —
forbidden, crashes phone). Since launchd did load and run far enough
to do sysctls/Mach calls, dyld must be finding libSystem from
somewhere (most likely the cryptex cache works for basic loading).
The failure remains a userspace check after /dev/console open; this
inspection found NO smoking gun in the form of a missing binary or
version mismatch. Remaining lead: whether the OS cryptex mount is
complete / its dyld cache fully accessible at launchd exec time.

Restoration (done by subagent): phone rebooted to fastboot via
console reboot-fastboot (confirmed xnuloader fastboot). /etc/rc
restored (cp rc.launchd root/etc/rc, 88 B, exec /sbin/launchd.darwin),
root.apfs rebuilt minimal mkapfs — 14 files, Nov 8 09:57, 4194304 B.
Launchd-test configuration restored. No writes to Darwin volume; no
flash/erase; /sbin/launchd never run from shell; cryptex path never
listed.

## Direction (b) — bootstrap-port BOOT TEST (2026-11-08 ~10:01-10:05)

Kernel md5 32b26634 (bootstrap helper). Flash-root + /etc/rc boot.
Fresh gui log 1284 lines (mtime Nov 8 10:02), run-log
diag-bootstrap-boot-run.log.

RESULT: HYPOTHESIS RULED OUT. Launchd behaves IDENTICALLY.

Evidence:
- Line 778: "PICASSO-DIAG: PID 1 bootstrap port set:
  port=0xfffffff60a9b6700 name=259 kr=0" — the helper WORKED.
  HakiOS init task got a valid bootstrap port (kr=0).
- Lines 781-783: HakiOS init special-port calls show
  bootstrap=0xfffffff60a9b6700 (valid, non-null).
- Lines 1231-1232: after exec, launchd's special-port calls show
  bootstrap=0xffffffffffffffff — NOT the valid port. The exec
  (HakiOS init -> launchd.darwin) does NOT preserve itk_bootstrap;
  launchd sees a sentinel/invalid value instead.
- Line 1236: "PICASSO-DIAG: PID 1 (launchd) called reboot(),
  opt=0x8, initproc_spawned=0" — launchd STILL reboots before
  main(), with the SAME 6-operation sequence (sysctlbyname
  hw.memsize, sysctlbyname kern.maxvnodes, task_set_special_port
  HOST_PORT rc=0, task_get_special_port ACCESS_PORT NULL, open
  /dev/console fd=3, reboot).

Conclusion: providing a bootstrap port for PID 1 does NOT change
launchd's behaviour. The bootstrap port is NOT the blocker — and
even the delivery mechanism fails, because exec resets
itk_bootstrap. A bootstrap fix would have to happen at exec time
(in load_init_program / exec_activate_image), not in
bsd_utaskbootstrap — but since launchd reboots BEFORE requesting
the bootstrap port at all, that would not help either. The failure
remains a PURE USERSPACE check in dyld/libSystem init after
/dev/console opens.

Phone after test: HALTED (kernel idle loop, /dev/ttyACM0 alive,
fastboot devices empty) — same as all previous launchd boots.

## Status after all 3 parallel directions (2026-11-08 ~10:05)

(a) Mach trace (parts 1+2): DONE. Launchd makes exactly 6 kernel
interactions, all succeeding, then reboots. Failure is a pure
userspace check; kernel tracing reached its limit.
(b) Bootstrap port: RULED OUT (this boot test). Helper worked for
the HakiOS init but exec does not preserve the port, and launchd
reboots identically.
(c) Darwin inspection: DONE. No version mismatch (iOS 16.5/20F66 =
Darwin 22.5.0); no missing binary (launchd.darwin 525 KB,
SpringBoard 112 KB, backboardd 1.18 MB all exist — corrects the
04:18 "binary absent" impression); dyld shared cache ABSENT from
all standard paths and libSystem absent as individual files
(consistent with iOS 16 cryptex design, but cryptex-mount
completeness unverified).

Remaining lead: whether the OS cryptex mount is complete / its dyld
cache fully accessible at launchd exec time. Next candidates:
(1) flattened diagnostic root (ios-sweet style — OS volume + both
cryptexes in one volume, no cryptex mount at boot) to eliminate
cryptex as a variable; (2) tracer part 3 (mach_msg,
shared_region_map_and_slide, csops) for PID 1; (3) AMFI/trust-cache
check. Awaiting user direction.

## Direction (2) — AMFI / trust-cache check (2026-11-08 ~10:20-10:35, subagent, k60 read-only, NO phone boot)

Question: is code signature / AMFI trust why launchd reboots before
main()? Verdict: NO — not a plausible cause (see the decisive
argument at the end).

(a) Is a trust cache loaded / is AMFI enforced in this build?
- NO trust cache is loaded. HakiOS replaces Apple's AMFI with its
  own stub "miniamfi" (xnu/files/libkern/amfi/miniamfi.c):
  line 126 comment: "libTrustCache: there are no trust caches, so
  nothing loads and nothing is found".
  mini_tc_loadModule (line 129) -> kTCReturnUnsupported;
  mini_tc_load (line 136) -> kTCReturnUnsupported;
  mini_tc_query (lines 144-148) -> kTCReturnNotFound, ALWAYS;
  mini_tc_checkRuntimeForUUID (line 194) -> kTCReturnNotFound.
  miniamfi_init (line 260) registers the interface at
  STARTUP(EARLY_BOOT, STARTUP_RANK_FIRST).
- Runtime confirmation in the boot log (build/wifi_scan_gui.log,
  (b) boot test kernel 32b26634): line 62 "calling mpo_policy_init
  for AMFI", line 63 "Security policy loaded: Apple Mobile File
  Integrity (stand-in) (AMFI)". The kernel itself announces AMFI
  is a STAND-IN.
- The boot log has NO "attempting to load external trust cache
  modules" / "completed loading external trust cache modules"
  lines — no trust cache modules were loaded at boot.
- The static trust cache comes from the device tree
  (bsd/kern/kern_trustcache.c:758 load_static_trust_cache():
  SecureDTLookupEntry "chosen/memory-map" -> SecureDTGetProperty
  "TrustCache"). The UEFI bootloader does not provide it. If it
  did provide a STATIC one, miniamfi's loadModule returning
  Unsupported would PANIC (kern_trustcache.c:878 "failed to load
  static trust cache module") — the kernel boots fine, so no
  static trust cache is provided. No trust cache files in the
  ramdisk (userland/root has none).
- Code-signing enforcement machinery IS present (bsd/kern/kern_cs.c,
  kern_codesigning.c) and miniamfi answers entitlement queries from
  each binary's own signature (miniamfi_entitlements.cpp). But
  without a trust cache, enforcement relies on CMS signature-chain
  verification, not a trust-cache allowlist. Apple-signed binaries
  pass. Enforcement is NOT blocking launchd (see verdict).
- Boot-args: the base CMDLINE (tools/boot_wifi_gui_test.py ~30-36)
  has NO AMFI-related args at all. But this XNU DOES recognise
  amfi_get_out_of_my_way / amfi_allow_any_signature /
  cs_enforcement_disable: the strings are present in the built
  kernel (build/out/hakios-kernel, md5 32b26634), so the parsing
  code is compiled in (=> !SECURE_KERNEL). cs_enforcement_disable
  (bsd/kern/kern_cs.c:167) is inside #if !SECURE_KERNEL AND gated
  by PE_i_can_has_debugger() (kern_cs.c:167-172). amfi_get_out_of_
  my_way (bsd/kern/kern_codesigning.c:73-100) sets
  CS_CONFIG_ALLOW_ANY_SIGNATURE with no debugger gate.
  launchd_unsecure_cache=1 was already tried as EXTRA_ARGS and did
  NOT fix it (it is a userspace dyld/launchd arg, not a kernel arg
  — its string is NOT in the kernel binary).

(b) launchd.darwin code signature: NOT directly inspectable from
k60. No copy of launchd.darwin exists on k60 (only on the phone's
Darwin volume, 525088 B, verified by direction (c)); no donor IPSW
(20F66__iPhone10,3_6) found on k60 in /root/hakios-picasso or
/root/hakios-u; the repo has only test launchd plists
(upstream/xnu-8796.121.2/tests/launchd_plists). It is a stock
Apple-signed binary from the iPhone X donor IPSW (iOS 16.5,
build 20F66). Its signature is a standard Apple CMS signature and
does not need the trust cache to validate (see verdict).

(c) VERDICT: AMFI / trust-cache is NOT a plausible cause of the
silent pre-main reboot. Decisive argument: launchd's code RUNS —
it makes 6 kernel interactions (sysctlbyname hw.memsize,
sysctlbyname kern.maxvnodes, task_set_special_port HOST_PORT,
task_get_special_port ACCESS_PORT->NULL, open /dev/console fd=3,
reboot RB_HALT). For launchd's code to execute at all, dyld must
have successfully loaded launchd.darwin AND libSystem. If code
signing enforcement or dyld-cache loading had failed, dyld would
have aborted the process BEFORE any of launchd's code ran (dyld
fatal error / SIGABRT / exec refusal) — launchd would never have
made those syscalls. The kernel trace shows launchd DID run and
deliberately called reboot(). Therefore code signing / dyld loading
SUCCEEDED, and the failure is in launchd's own early userspace
init AFTER dyld finished. Supporting points: the trust cache is
absent BY DESIGN (miniamfi stub), and the kernel's shared-cache
mapping has a root-ownership fallback (bsd/vm/vm_unix.c:2437
query_trust_cache -> on NotFound goto root_check, file must be
owned by root) — a missing trust cache does not block the dyld
cache. Caveat: mach_msg was NOT traced, so an (unlikely) early
mach_msg from launchd to some service would be invisible; there is
no evidence launchd talks to amfid during early init (amfid is a
daemon launchd itself starts later).

SINGLE TEST to prove/rule out definitively (cheap, safe,
reversible — boot-arg only, no rebuild, no flashing, RAM-boot):
boot with HAKIOS_EXTRA_ARGS='wifi=0 root_apfs=ufs_darwin
amfi_get_out_of_my_way=1'. If launchd still reboots identically
(same 6 ops), AMFI is definitively ruled out. If launchd proceeds
past /dev/console, AMFI was the blocker. Prefer
amfi_get_out_of_my_way=1 over cs_enforcement_disable=1 (the latter
is gated by PE_i_can_has_debugger). Do NOT re-test
launchd_unsecure_cache=1 alone — already tried, did not fix it.

## Direction (1) — tracer part 3, BUILD READY (2026-11-08 ~10:40, subagent)

User said "Làm hết" (do all three); this is direction (1), run in
parallel with (2) AMFI and (3) flattened root. Build only — phone
NOT booted (main agent coordinates boot tests).

Three new PICASSO-DIAG tracers, all gated on PID 1 only, non-fatal,
2-4 lines each, following the parts 1+2 pattern:

1. mach_msg — osfmk/ipc/mach_msg.c, mach_msg_overwrite_trap(),
   inserted before the `end:` return (line ~1034, after
   ipc_port_thread_group_unblocked()): logs option + return value.
   `PICASSO-DIAG: PID 1 mach_msg_overwrite_trap(option=0x%x) -> mr=%d`
   (option32 and mr in scope at that point; <kern/task.h> included.)
2. shared_region_map_and_slide — osfmk/vm/vm_shared_region.c,
   vm_shared_region_map_file() (line ~1934; the function dyld uses
   via the shared_region_map_np() syscall to populate the shared
   region with the shared cache — the actual
   shared_region_map_and_slide_2_np syscall has no separate
   definition in this tree, only a comment reference at line 62).
   Inserted before `return kr;` (line ~2089): logs return value.
   `PICASSO-DIAG: PID 1 vm_shared_region_map_file -> kr=%d`
   (task_pid(current_task()) already used in this file, e.g. line
   2147.)
3. csops — bsd/kern/kern_proc.c, csops() (line 3078; the syscall
   implementation lives in kern_proc.c, NOT kern_cs.c). Restructured
   the one-line wrapper to capture the return value, then log ops +
   error, gated on proc_getpid(p)==1.
   `PICASSO-DIAG: PID 1 csops(ops=0x%x) -> error=%d`

Notes: first ./xnu/export.sh run hung in an SSH session (~8 min,
killed; patch still at 16 lines); re-ran with `timeout 240` and it
completed in ~10 s (EXPORT-EXIT:0).

Build: kernel md5 1368bbc4c4a295cd8c21734b70127ff2 (was 32b26634),
BUILD-EXIT:0, grep -c 'error:' = 0, size 15674192 B (unchanged —
expected, same as all 2026-11-08 diagnostic rebuilds), timestamp
Nov 8 10:40. CC mach_msg.o + CC vm_shared_region.o + CC kern_proc.o
all compiled. xnu/hakios.patch now has 19 PICASSO-DIAG lines
(was 16).

Boot test (PENDING, main agent): flash-root + /etc/rc. Watch for
the three new lines from PID 1. If launchd calls
vm_shared_region_map_file and kr != 0, the dyld shared cache map
failed — smoking gun for the cryptex/dyld-cache lead. If it calls
mach_msg or csops and either fails, that is the failing userspace
check. If none of the three fires before reboot, the check is in a
call not yet traced. (Per the AMFI direction's plan, this boot can
also carry amfi_get_out_of_my_way=1 to test AMFI in the same run.)

## Combined boot test — tracer part 3 + AMFI (2026-11-08 ~10:41-10:46)

Kernel md5 1368bbc4 (tracer part 3) + HAKIOS_EXTRA_ARGS='wifi=0
root_apfs=ufs_darwin amfi_get_out_of_my_way=1'. Flash-root + /etc/rc.
Fresh gui log 74323 B (mtime Nov 8 10:45), 1284+ lines, run-log
diag-tracer3-amfi-run.log.

RESULTS:
- Tracer part 3: ZERO of the three new tracers fired. No
  mach_msg_overwrite_trap, no vm_shared_region_map_file, no csops
  from PID 1 in the entire log. Launchd does NOT call any of these
  before rebooting.
- AMFI: DEFINITIVELY RULED OUT. With amfi_get_out_of_my_way=1,
  launchd STILL reboots identically — same 6 operations
  (sysctlbyname hw.memsize, sysctlbyname kern.maxvnodes,
  task_set_special_port HOST_PORT rc=0, task_get_special_port
  ACCESS_PORT NULL, open /dev/console fd=3, reboot RB_HALT,
  initproc_spawned=0). The AMFI diagnostic arg changed nothing.
- Phone halted after test (kernel idle loop, /dev/ttyACM0 alive).

INTERPRETATION: launchd's early init makes NO kernel call beyond
the 6 already known — not mach_msg, not shared-region mapping, not
csops, not mach_port_allocate, not sysctl, not openat. The failing
check is therefore either (a) a PURE USERSPACE computation (a
version/feature comparison, a boot-arg or environment check, or a
dyld-internal check on data already in memory), or (b) a syscall
not yet traced (mmap, statfs, sandbox_init, getuid, ...). Note on
vm_shared_region_map_file: its absence does NOT prove the dyld
cache was unmapped — the cache may be mapped at exec time by the
kernel rather than via a userspace call from launchd; it only
proves launchd/dyld does not call it during early init. The
cryptex/dyld-cache lead is now narrowed to cache CONTENTS /
slide / version rather than loading. The flattened-root test
(direction 3, still running) is now the key remaining experiment:
if launchd proceeds from a flattened root, cryptex/dyld-cache is
the problem; if it still reboots identically, cryptex is ruled
out too.

## Flattened root (direction 3) — BUILT 2026-11-08 ~11:00 UTC

**Goal**: merge the OS volume + both cryptexes into ONE volume so launchd runs WITHOUT any cryptex mount at boot (ios-sweet approach, adapted to HakiOS/APFS). Diagnostic: if launchd gets past the pre-main reboot from the flat root, cryptex was the problem; if it still reboots identically, cryptex is ruled out.

**Why this is the key remaining experiment**: tracer part 3 + AMFI combined boot test (03:41-03:46 UTC) ruled out AMFI definitively and showed launchd makes NO kernel call beyond the 6 known ops (no mach_msg, no shared_region_map_and_slide, no csops). Cryptex/dyld-cache lead narrowed to cache CONTENTS/slide/version rather than loading. The flat root is the decisive test.

**Layout map (from code + images)**:
- Boot today (flash-root): init.c flash_root mounts /dev/ufs_darwin at /flash (MNT_RDONLY), binds ramdisk dirs, mounts cryptexes OS (",c=1") and App (",c=2") via mount_cryptex (md0 containers first, else flash device), switches root via kern.hakios_switch_root.
- BOTH cryptex mounts FAIL at boot (gui log lines 823-826): "the OS cryptex from /dev/ufs_darwin,c=1: Invalid argument" / "[FAIL] the OS cryptex: in neither md0 nor the flash" (same for App). Root cause: root.apfs (md0) has only 1 container (no ,c=1/2), and the phone's flash APFS container has no containers 1/2 either. flashboot.apfs (which would carry the cryptexes) was never built.
- Donor sysimage: id-it-tritm-2 has the full 20F66 donor (rootfs 2.9 GB, cryptex 26 MB, cryptex-app 11 MB, dyld cache 2.6 GB / 43 files). Restored to k60 sysimage/: cryptex/, cryptex-app/, 20F66__iPhone10,3_6/ (rsync from tritm@100.67.179.75; fixed a nesting pitfall where rsync without trailing slash created cryptex/cryptex/).
- system.apfs (the built base Darwin volume, 2.47 GB compressed) was extracted with a new bulk extractor /root/flatroot/extract_apfs.py (uses apfs.py API, root OID 2 = ROOT_DIR_INO_NUM, NOT 1029): 45044 files / 16547 dirs / 671 symlinks, 0 errors, 5.5 GB decompressed.

**Flattened root composition** (/root/flatroot/sysroot/, 8.1 GB decompressed):
- Base: extracted system.apfs (45044 files).
- OS cryptex overlay: sysimage/cryptex/ (1412 files, 616 dirs, 1 link, 31 type-replaced — base had framework STUBS (files/symlinks) where cryptex has real dirs; a plain `cp -a` fails on these, used /root/flatroot/merge.py which replaces on type conflict). Only 2 file conflicts (SystemVersion.plist, RestoreVersion.plist) — cryptex wins.
- App cryptex overlay: sysimage/cryptex-app/ (884 files, 147 dirs, 9 type-replaced, 0 file conflicts).
- Dyld cache: sysimage/20F66__iPhone10,3_6/ (43 files) → System/Library/Caches/com.apple.dyld/. Cache compresses ~3:1 with zlib (main .01 file 109 MB → 36 MB), so it adds only ~0.8 GB to the image.
- Boot files from root.apfs (extracted to /root/flatroot/bootfiles/): /sbin/init (HakiOS init 85800 B), /sbin/launchd → init (symlink, REPLACES stage0), /etc/rc + /etc/motd (via /etc → private/etc symlink — apfs.py `cat /etc/rc` says "no such path" because it doesn't resolve the symlink; the file IS at /private/etc/rc, 88 B, "exec /sbin/launchd.darwin"), /bin/child + /bin/var_store, /redwood/LaunchDaemons/com.hakios.varstore.plist, /fw/ GPU firmware (7 files), mount-point dirs /empty /hal /share /flash /xpc /Cryptexes/OS /Cryptexes/App.

**Built image**: /root/flatroot/flat.apfs — 851698 blocks x 4096 B = 3,488,555,008 B (3.49 GB), volume name "flatroot", 47394 files / 17322 dirs / 641 symlinks. Built with `python3 mkapfs.py -z --plain fw -o /root/flatroot/flat.apfs flatroot=/root/flatroot/sysroot` (run from tools/apfs/, ~7 min, 6 parallel workers). Verified with apfs.py: /sbin/init + launchd→init + launchd.darwin (525088 B), /System/Library/Caches/com.apple.dyld/ (43 files), /private/etc/rc (exec /sbin/launchd.darwin), SpringBoard, backboardd (1182960 B), libSystem.B_asan.dylib. NOTE: /etc is a symlink → private/etc (Darwin firmlink); rc/motd live at /private/etc/.

**SIZE / RAM**: 3.49 GB image. Phone has ~4 GB free after kernel boot with a 4 MB ramdisk, so a 3.49 GB ramdisk leaves only ~700 MB free. TIGHT but should fit for a diagnostic (launchd pre-main test only). If the phone OOMs or fails to stage, that's a size problem, not a launchd result — fall back to flashboot.apfs (build.sh FLASHBOOT=1, ~1 GB) or trim the flat root.

**Boot command (main agent runs this; do NOT run from a subagent)**:
1. Phone must be in fastboot (xnuloader). Kernel is the current tracer kernel (md5 1368bbc4) — already built; do NOT rebuild.
2. Swap the ramdisk (BACKUP root.apfs first):
   `cd /root/hakios-picasso/build/hakios-xnu-kernel/userland && cp root.apfs /root/flatroot/root.apfs.backup && cp /root/flatroot/flat.apfs root.apfs`
   (The boot script hardcodes `ramdisk={KERNEL_WS}/userland/root.apfs` at tools/boot_wifi_gui_test.py:85 — no env override exists.)
3. Boot (NO root_apfs — ramdisk root; wifi + GPU off to save RAM and isolate the variable):
   `cd /root/hakios-picasso && HAKIOS_EXTRA_ARGS='wifi=0 adreno=0 gpu=0' python3 tools/boot_wifi_gui_test.py`
   Base CMDLINE already has rd=md0 (no root_apfs in the base). init runs from /sbin/init (via /sbin/launchd symlink), reads /etc/rc, execs /sbin/launchd.darwin.
4. Wait ~100-120 s, read build/wifi_scan_gui.log (verify FRESH mtime).
5. RESTORE root.apfs afterwards: `cp /root/flatroot/root.apfs.backup /root/hakios-picasso/build/hakios-xnu-kernel/userland/root.apfs`

**Pass vs fail**:
- PASS (cryptex was the problem): launchd proceeds past the 6-op sequence — initproc_spawned=1, OR any new tracer line fires (mach_msg / vm_shared_region_map_file / csops / mach_port_allocate), OR launchd requests TASK_BOOTSTRAP_PORT (which=4), OR LaunchDaemon/backboardd/SpringBoard activity appears.
- FAIL (cryptex ruled out): launchd reboots IDENTICALLY to the flash-root baseline — same 6 ops (sysctlbyname hw.memsize, kern.maxvnodes, set HOST_PORT, get ACCESS_PORT NULL, open /dev/console fd=3, reboot RB_HALT opt=0x8, initproc_spawned=0), no new tracer lines.
- INCONCLUSIVE (size): loader/kernel fails to stage or boot the 3.49 GB image (OOM, no init output) — then try flashboot.apfs or a trimmed flat root.

**Fallback not built**: flashboot.apfs (build.sh FLASHBOOT=1) — the DESIGNED recipe (boot volume + remade OS cryptex w/ cache + App cryptex, ~1 GB). NOT built because running build.sh userland would regenerate root.apfs/system.apfs and could interfere with the main agent's ongoing boot tests. It supplies the missing cryptex via the mount path (md0,c=1) rather than eliminating the mount; build it only if the flat root doesn't fit or as a second data point.

**Scratch**: /root/flatroot/ (extract_apfs.py, merge.py, sysroot/ 8.1 GB, bootfiles/, flat.apfs 3.49 GB, extract.log, mkapfs.log). No repo files modified (flat.apfs lives outside the git tree). No phone interaction by this subagent.

## UEFI update — 2026-11-08 ~11:10-11:25 (subagent, build/prep only, no phone boot)

**Finding (quan trọng): KHÔNG có Mu-Silicium UEFI firmware trong cây xnu-kernel.** `git ls-tree -r HEAD --name-only | grep -i uefi` chỉ ra đúng 1 file: `docs/uefi-boot.md`. Không có thư mục `uefi/` ở top-level, không có file Silicium/Mu- nào trong cây. Lịch sử git xác nhận UEFI đã được tách ra thành repo riêng ("the SoC, device and UEFI repositories are their own", split-repos.sh). `xnuloader/` (loader.efi) thì ĐÃ nằm trong cây và build.sh `build_kernel_and_loader` ĐÃ build nó từ cây rồi — không có gì để chuyển ở phần này.

**Cái "mới" thực sự trong cây:** `devices/manifest.conf` trong cây 63fc2b4 pin `hakios-uefi` ở revision `1c6522893266157506c767e2ca427053ed3634a8`, trong khi build.sh đang pin `687fa6c6a5a70f43184de92980e0a76eea5f2c4b`. 1c65228 là 4 commit sau 687fa6c, trên branch `origin/mac`, và cả 4 commit CHỈ thêm U-Boot (u-boot/BASE, apply.sh, build.sh, export.sh, files/cmd/hakios.c, files/lib/efi_loader/efi_qcom_usb.c, hakios.patch, README). `git diff 687fa6c 1c65228 -- hakios.patch files branding` = EMPTY — firmware Mu-Silicium (hakios.patch, files/, branding/) giống hệt nhau, nên SILICIUM_UEFI.fd build ra sẽ giống hệt bản prebuilt Nov 5.

**Đã làm:**
- build.sh line 39 (integrity array): `hakios-uefi:687fa6c...` → `hakios-uefi:1c65228...`
- build.sh line 137 (prepare_workspaces): checkout pin `687fa6c...` → `1c65228...`
- patches/UPSTREAM: hakios-xnu-kernel `96ffbc5` → `63fc2b4`; hakios-uefi `687fa6c...` → `1c65228...` (hakios-metal để nguyên cho agent Metal)
- Workspace `build/hakios-uefi` đã checkout sang 1c65228 (đã có sẵn origin/mac). `build/Mu-Silicium` chưa tồn tại nên lần build_uefi tới sẽ prepare mới từ đầu.
- KHÔNG bỏ clone hakios-uefi (build_uefi cần $BUILD_DIR/hakios-uefi/hakios.patch, files/, branding/banner.py; không có bản thay thế trong cây — bỏ đi sẽ phá build_uefi).

**Kiểm chứng:** `bash -n build.sh` OK; grep `687fa6c` trong build.sh = 0, trong patches/UPSTREAM = 0; workspace tại 1c65228; firmware files giống hệt nhau (diff empty). KHÔNG chạy full build_uefi (EDK2 nặng, input giống hệt bản Nov 5 đã build thành công nên output sẽ giống hệt; tránh xung đột CPU với agent Metal + flattened-root).

**Còn tồn đọng (cho main agent):** build.sh vẫn còn 2 pin xnu-kernel cũ `96ffbc5` chưa sửa — line 77 (prepare_workspaces checkout xnu-kernel) và line 245 (export_patches BASE_SHA). Không sửa trong task này để tránh xung đột với agent Metal (cùng sửa build.sh). Cả hai đều là latent bug cho clean build (checkout sai base), nên sửa lên `63fc2b4` khi tiện.

## Flat root boot test — id-it-tritm-2 path BLOCKED, streaming build on k60, stage FAILED (63 MB fastboot limit) — 2026-11-08 ~11:11-11:20 UTC

### id-it-tritm-2 unreachable from k60
- k60 has NO Tailscale (no tailscaled process, no tailscale0 interface, no 100.64.0.0/10 route, no tailscale/tailscaled binaries). `ssh tritm@100.67.179.75` times out; ping 100% loss; MagicDNS does not resolve. LAN probe: no SSH on 192.168.1.64/.248/.115.
- Muse VM CAN reach id-it-tritm-2 via Tailscale proxy (TCP connect to :22 succeeded, "Empty reply"), so id-it-tritm-2 is ONLINE — the break is k60-side (Tailscale not running on k60). User direction "push to id-it-tritm-2" could not be followed as specified.
- Workaround used instead: streaming payload builder on k60 (see below). Original mkpayload.py NOT modified; build.sh NOT modified; root.apfs untouched (4 MB).

### Streaming mkpayload (verified byte-identical)
- New script /root/flatroot/mkpayload_stream.py: replicates mkpayload.py format (header magic 0x50554E58, entries '<16sII' + data + 16B padding, 24B trailing zeros) but streams file items in 1 MB chunks and precomputes body length, so RAM use is O(1).
- Verification: built payload for the small root.apfs (4 MB) with BOTH scripts — both 19940784 B, cmp IDENTICAL, md5 bb9b71d51afed5080a842c6fc10bd59b.
- Flat payload built on k60: /root/flatroot/payload.bin, 3504301488 B, md5 2e8b718b46bfb9788c7d6b8d7697d363. Cmdline = base CMDLINE + "wifi=0 adreno=0 gpu=0" (same as the failed k60 run). Kernel 1368bbc4, loader.efi, kernel.ver 2.3.4-hakios-darwin22.5-sm7250-g63fc2b46-dirty. No OOM (host free stayed ~5.3 GB available).

### Stage FAILED — phone fastboot max-download-size is 63 MB
- `fastboot stage /root/flatroot/payload.bin` → "fastboot: error: cannot load '/root/flatroot/payload.bin'" (exit 1).
- `fastboot getvar max-download-size` → 0x03f00000 = 63 MB. A 100 MB dummy file also fails with the same "cannot load" error, confirming the limit (normal ~20 MB payloads stage fine).
- CONSEQUENCE: the flat payload (3.5 GB) CANNOT be staged via fastboot on this phone. The phone was NEVER booted with flat.apfs; gui log is STALE (10:45 tracer test). Result: INCONCLUSIVE (payload stage fail). The launchd/cryptex hypothesis is NOT tested by this attempt.
- WARNING for the flashboot.apfs fallback (~1 GB): a ~1 GB payload also exceeds 63 MB, so it will ALSO fail `fastboot stage` unless it is loaded by a different mechanism (not as the payload ramdisk). The flashboot path needs re-examination against this limit.
- Phone state: xnuloader fastboot (untouched by the failed stage). k60 root.apfs still 4 MB (md5 559d580e). No flash/erase/UFS write.

## Flash flat root to UFS Darwin partition — BLOCKED, needs ABL fastboot (2026-11-08 ~12:15-12:30)
- User authorized UFS flash 11:26 ("Build thử rồi flash đi. Cần thì cứ flash vào ufs. Tôi có sẳn phân vùng darwin được tách từ userdata rồi").
- Partition name VERIFIED: "darwin" (/dev/ufs_darwin in kernel log; dual-boot.md `fastboot flash darwin ...`; UFS driver). Size: documented 20 GiB (dual-boot.md: partition darwin, 20 GiB at end of LUN 0; userdata-carve.py invoked with "20"; UFS_DARWIN_MIN=5242875 blocks x 4096 = 20 GiB). Exact byte size NOT measured — boot with ufs_write=1 printed "the loader offers no partition to write into" / "darwin is not among the partitions" (xnuloader does not offer a write partition; a loader limitation, not a size problem). flat.apfs 3.49 GB << 20 GiB, fits with large margin.
- Sparse-split DONE: /root/flatroot/flat.apfs (3,488,555,008 B, sha256 c54183e6...) split by tools/sparse-split.py (MAX_BYTES=66060288 = 63 MB) into /root/flatroot/pieces/ — 54 pieces (piece-00..53.simg), max 65,011,776 B < 66,060,288 B max-download-size, total 3.3 GB, SPLIT-EXIT:0. sha256 of flat.apfs verified matches split input.
- FLASH BLOCKED: xnuloader fastboot does NOT support `fastboot flash` — `fastboot flash darwin pieces/piece-00.simg` returns "Writing 'darwin' FAILED (remote: 'unknown command')". xnuloader fastboot server (loader.c:3635-3641) only supports: stage, oem run, getvar, get_staged, oem hang, reboot, download, oem ramdisk. NO flash/erase/UFS-write command.
- NEXT STEP NEEDS USER: enter ABL fastboot (Volume Down + Power) — ABL fastboot supports `fastboot flash`. Then flash the 54 pieces in order (`fastboot flash darwin pieces/piece-XX.simg`) and boot from the Darwin partition to test launchd. Phone currently in xnuloader fastboot; pieces ready; k60 root.apfs untouched (4 MB).

## Flat-root UFS flash — PASS (cryptex hypothesis CONFIRMED) — 2026-11-08 ~14:03-14:16

User entered ABL fastboot (Volume Down + Power) at ~14:03. This unblocked the flash
that xnuloader fastboot could not do (no `fastboot flash` command).

### ABL fastboot verification
- `fastboot devices`: f9f3d7b0 fastboot; `product: picasso`
- `max-download-size: 805306368` (768 MB — vs xnuloader 63 MB). Confirms ABL, not xnuloader.
- `partition-size:darwin: 0x5000FB000` (~20 GiB). Darwin partition exists, flat.apfs fits easily.

### Sparse pieces verified before flashing
- All pieces Android sparse magic 0xed26ff3a, blk_sz 4096, total_blks 851698
  (= 3,488,555,008 B = flat.apfs size) in every piece.
- Chunk walk confirms correct offsets: piece-00 writes blk 0..15871 then DONT_CARE;
  piece-01 starts with DONT_CARE blk 0..15871 then writes 15872..31743; piece-53
  DONT_CARE blk 0..841215 then writes 841216..851697. Sequential flash reconstructs
  the full image; pieces do NOT overwrite each other.

### Flash result: 54/54 OK
- `fastboot flash darwin piece-XX.simg` for piece-00..53, all exit=0.
- Log /root/flatroot/flash.log: 108 OKAY (54 Sending + 54 Writing), 0 FAILED.
- No bytes were written by the earlier xnuloader attempt; this ABL flash is the first
  successful UFS write of flat.apfs to `darwin`.

### Reboot to xnuloader + boot
- `fastboot reboot` OKAY; after 45 s `fastboot devices` shows `xnuloader fastboot`.
- Boot: `HAKIOS_EXTRA_ARGS='wifi=0 adreno=0 gpu=0 root_apfs=ufs_darwin'
  python3 tools/boot_wifi_gui_test.py` (build/flatroot-ufs-run.log). Kernel 1368bbc4
  (tracer part 3). Payload 19,940,800 B staged OK; `fastboot oem run` OKAY.
- Gui log build/wifi_scan_gui.log FRESH: mtime 2026-11-08 14:13:49, 311,432 B, 4968 lines.
  Boot launched 14:09:48. Phone alive after boot (/dev/ttyACM0 exists, kernel
  memory/buttons stats continue, fastboot devices empty).

### Result: PASS — launchd runs normally; cryptex/dyld-cache WAS the cause
- Flat root mounted: `apfs: volume 0 "flatroot" of container 0: 47394 files,
  17322 directories` (matches flat.apfs build). Root = /dev/ufs_darwin.
- Cryptex mounts STILL fail (OS ,c=1 / App ,c=2: Invalid argument) — but irrelevant
  now: flat root already contains both cryptexes merged in.
- Launchd goes FAR past the 6-op baseline. PICASSO-DIAG open tracer shows PID 1
  opening: /System/Library/UserManagement/com.apple.xpc.launchd.personas.plist (ok),
  /etc/master.passwd (ok), /etc/group (ok), /redwood/LaunchDaemons (ok, fd=4),
  /System/Library/LaunchAngels, /System/Library/NanoLaunchDaemons,
  /System/Library/DeveloperModeLaunchDaemons (all ok), plus numeric sysctl() calls
  (name0=0 name1=3, name0=1 name1=65, name0=6 name1=144 — ZERO numeric sysctl in baseline).
- xpc.launchd log (78 lines): "entering bootstrap mode" / "exiting bootstrap mode" /
  "exiting ondemand mode", spawning com.hakios.varstore via xpcproxy (pid 7),
  var_store ran and exited 0. Some services fail to import (error 152, "cannot load
  in current boot environment": axAuditDaemon, instruments, sysmond) and
  /AppleInternal paths missing (error 2) — non-fatal launchd-level issues.
- NO reboot(RB_HALT) from PID 1: grep for `reboot(` and `RB_HALT` returns nothing;
  no PICASSO-DIAG reboot line. Phone stays alive (no halt, no fastboot).
- backboardd (1182960 B) and SpringBoard.app exist; launchd opened
  /System/Library/SpringBoardPlugins (ok). But NO backboardd/SpringBoard process
  spawn lines in this log window — they have not started yet. Next step for Phase 4
  is to find why backboardd/SpringBoard do not spawn (or wait longer / check screen).

### State after test
- Phone: booted on flat root (UFS darwin), alive, /dev/ttyACM0 present, idle.
- k60 root.apfs untouched (4 MB, md5 559d580e). No build.sh change, no export-patches,
  no commit/push.

## 2026-11-08 ~14:22 — Why backboardd/SpringBoard have NOT spawned on flat root (subagent investigation)

**Log:** build/wifi_scan_gui.log FRESH (311,432 B, mtime 14:13, 4968 lines; unchanged at 14:22 check). Phone alive (/dev/ttyACM0 exists), NOT in fastboot, boot-script capture already exited. No new lines since 14:13 — phone idle, nothing spawned after varstore.

**Launchd bootstrapped exactly ONE job:** com.hakios.varstore (xpcproxy pid 7, var_store exit 0, now inactive/not running). Bootstrap errors (non-fatal): /AppleInternal paths error 2 (both domains); error 152 "Service cannot load in current boot environment" for 3 DeveloperModeLaunchDaemons (axAuditDaemon, instruments, sysmond); kpersona type-6 warnings x2. NO other job spawned; NO backboardd/SpringBoard lines.

**Backboardd/SpringBoard mentions in log (16 lines, all listings, ZERO spawn):** binary /usr/libexec/backboardd (1,182,960 B) + /flash/usr/libexec/backboardd exist; SpringBoardPlugins dir opened by launchd (err=0); /System/Library/CoreServices/SpringBoard.app exists. No "com.apple.backboardd" / "com.apple.SpringBoard" job lines at all.

**Plists in flat root sysroot (/root/flatroot/sysroot/) — ROOT CAUSE:** com.apple.backboardd.plist and com.apple.SpringBoard.plist DO NOT EXIST anywhere in sysroot (find across whole tree: only FeatureFlags/Domain/SpringBoard.plist + Preferences/Logging/Subsystems/com.apple.SpringBoard.plist, not LaunchDaemon plists). /System/Library/LaunchDaemons is a SYMLINK -> /redwood/LaunchDaemons (HakiOS redirect in base system.apfs); /redwood/LaunchDaemons contains ONLY com.hakios.varstore.plist (908 B). /Library/LaunchDaemons has only com.apple.factory.NFQRCoded.plist. /System/Library/LaunchAngels is EMPTY. So launchd has NO job definitions for backboardd/SpringBoard (or any Apple system daemon) and cannot spawn them. Launchd itself works fine (spawned varstore); this is a flat-root BUILD defect, not a runtime failure.

**Why OS cryptex did not fix it:** merge.py DOES replace symlink->dir (31 type-replaced), so if the OS cryptex had a real /System/Library/LaunchDaemons/ it would have replaced the symlink. It did not, so the cryptex has no LaunchDaemons (in iOS 16 system LaunchDaemons live in the base volume, not the cryptex). The HakiOS base system.apfs carries the symlink instead of the real directory.

**Next step (most specific):** Rebuild flat root with the REAL Apple /System/Library/LaunchDaemons/ from the DONOR (original iOS 16.5 system.apfs, pre-HakiOS). Replace the symlink in sysroot with the donor directory (com.apple.backboardd.plist, com.apple.SpringBoard.plist + all other Apple system daemon plists); keep /redwood/LaunchDaemons/com.hakios.varstore.plist separate (launchd loads /redwood/LaunchDaemons directly, so varstore still loads). Rebuild flat.apfs, re-split, re-flash to Darwin, re-boot, verify backboardd/SpringBoard spawn. (Quicker test-only alternative: add just the two plists from donor, rebuild/re-flash/re-boot — but full directory is better, other daemons may be needed for a working GUI.) Console/ps NOT checked via pyserial — log + sysroot evidence conclusive; phone state preserved (no reboot/flash/build).

## Rebuild flat root with donor LaunchDaemons — BLOCKED, donor rootfs missing from k60 (2026-11-08 ~14:23-15:00)
- GOAL: replace sysroot /System/Library/LaunchDaemons symlink with the REAL Apple directory from the donor (iOS 16.5), rebuild flat.apfs, re-split, re-flash to Darwin, re-boot, verify backboardd/SpringBoard spawn.
- BLOCKER (step 1): donor rootfs NOT on k60. sysimage/ at build/hakios-xnu-kernel/sysimage/ has ONLY cryptex/ (26 MB), cryptex-app/ (11 MB), 20F66__iPhone10,3_6/ dyld cache (2.6 GB) + scripts + empty .done-rootfs marker. NO rootfs/ directory anywhere on k60 (find, maxdepth 4, whole /root /home). NO *.ipsw, NO *.dmg on k60. Donor rootfs (2.9 GB) lives only on id-it-tritm-2.
- VERIFIED no Apple LaunchDaemon plist anywhere on k60: find com.apple.backboardd.plist = 0 hits (whole /root). All root.apfs (current 559d580e, build/out 07:18 4e573a8d, hakios-u Nov 3 aa080d76) /LaunchDaemons = ONLY com.hakios.varstore.plist (mkjobs.py never generated DARWIN_JOBS — needs $DARWIN/rootfs). All system.apfs (build/out, /root/hakios, hakios-u) /System/Library/LaunchDaemons = symlink (resolves empty). Only Apple plists on k60: com.apple.factory.NFQRCoded.plist (Library/LaunchDaemons), NanoLaunchDaemonsAltAccount (5 plists), DeveloperModeLaunchDaemons instruments (1 plist).
- SSH to id-it-tritm-2 from Muse VM: Tailscale proxy (port 3130) REACHES 100.67.179.75:22, but key auth REFUSED (Permission denied publickey,password); tritm password was transient (not recorded) — NO credentials available to this agent. k60 cannot reach id-it-tritm-2 (no Tailscale, verified 11:20).
- NOT DONE: sysroot NOT modified (symlink intact, varstore intact), flat.apfs NOT rebuilt (still 3,488,555,008 B md5 a6e66231), pieces NOT re-split (still 54 old pieces), NO flash, NO boot. Phone untouched (still alive on flat root, Darwin partition).
- NEXT STEP NEEDS PARENT/USER: get the donor LaunchDaemons to k60 — either (a) user provides tritm password for one-time use so the LaunchDaemons dir (491 plists, few MB) can be copied from id-it-tritm-2 sysimage/rootfs/System/Library/LaunchDaemons/ to k60, or (b) user copies it another way. Once on k60: rm symlink in sysroot, copy donor dir in, keep /redwood/LaunchDaemons/com.hakios.varstore.plist, rebuild flat.apfs (mkapfs.py -z --plain fw, ~7 min), re-split (sparse-split.py, 63 MB max), then re-flash (needs ABL fastboot — user physical action) + re-boot.
- 2026-11-08 ~14:45-15:00 REBUILD flat root với LaunchDaemons thật — DONE (rebuild + re-split), BLOCKED ở flash (cần ABL fastboot — user vật lý). Donor LaunchDaemons đã có trên k60 tại /root/flatroot/donor-LaunchDaemons/ (491 file, md5 tar.gz f07b3cfb khớp). Sysroot đã sửa: symlink /System/Library/LaunchDaemons -> /redwood/LaunchDaemons đã XÓA, thay bằng thư mục thật (491 plist, có com.apple.backboardd.plist + com.apple.SpringBoard.plist); /redwood/LaunchDaemons/com.hakios.varstore.plist (908 B) còn nguyên. Rebuild flat.apfs: MKAPFS-EXIT:0, 47885 files (tăng đúng 491), 17323 dirs, 640 symlinks; flat.apfs mới 3,489,058,816 B, md5 b1fbd06dc9d4d02907346d210ec71e5e (cũ a6e66231). Verify apfs.py: /System/Library/LaunchDaemons trong image mới có cả hai plist, 491 entries. Re-split: SPLIT-EXIT:0, 54 mảnh tại /root/flatroot/pieces/ (ghi đè mảnh cũ), mảnh lớn nhất 65,011,776 B (< 66,060,288 B), tổng 3.3G, sha256 b84c8eb2 khớp flat.apfs mới. Điện thoại đã đưa về xnuloader fastboot qua console `reboot-fastboot` (fastboot devices: xnuloader fastboot, /dev/ttyACM0 không còn). KHÔNG vào được ABL fastboot từ xa (fastboot reboot bootloader từ xnuloader chỉ quay lại xnuloader — đã biết). **Cần USER: đưa điện thoại vào ABL fastboot (Volume Down + Power), rồi flash 54 mảnh mới lên `darwin`, reboot về xnuloader, boot với root_apfs=ufs_darwin, đọc log launchd.** Không sửa build.sh, không export-patches, không commit/push, không động root.apfs 4 MB.

## 2026-11-08 ~15:00-15:09 — FLASH flat.apfs v2 (donor LaunchDaemons) + BOOT — FAIL (backboardd/SpringBoard STILL not spawning)
- ABL verified: f9f3d7b0 fastboot, product picasso, max-download-size 768 MB, partition-size:darwin 0x5000FB000.
- Pieces verified: 54 (piece-00..53.simg); flat.apfs md5 b1fbd06d, sha256 b84c8eb2 — NEW build.
- Flash 54/54 OK: 108 OKAY, 0 FAILED, 0 nonzero exits (log /root/flatroot/flash-v2.log).
- fastboot reboot -> xnuloader fastboot OK. Boot HAKIOS_EXTRA_ARGS='wifi=0 adreno=0 gpu=0 root_apfs=ufs_darwin' (kernel 1368bbc4), run-log build/flatroot-v2-run.log, boot 15:05:41.
- Gui log FRESH: mtime 15:07, 311123 B, 4972 lines. APFS "flatroot" 47885 files / 17323 dirs — NEW image DID mount.
- Donor plists VALID (plistlib): backboardd RunAtLoad=True, ProgramArguments ['/usr/libexec/backboardd'], KeepAlive=True, Disabled=None; SpringBoard RunAtLoad=True, ProgramArguments ['/System/Library/CoreServices/SpringBoard.app/SpringBoard'], KeepAlive=True. Both should spawn at boot.
- RESULT: FAIL — backboardd/SpringBoard did NOT spawn. Launchd only spawned com.hakios.varstore (pid 7, exit 0). 16 lines match backboardd|SpringBoard — all file listings, ZERO spawn lines. No reboot(RB_HALT) (grep count 0) — launchd still runs normally. Log stopped growing (only kernel periodic stats). Phone alive, /dev/ttyACM0 present, idle.
- KEY EVIDENCE launchd does NOT see the donor dir: launchd opened /System/Library/LaunchDaemons (fd=4, err=0) twice, but got err=2 for /System/Library/LaunchDaemons/com.apple.jetsamproperties.D22.plist — yet D22.plist EXISTS in the donor dir (and in the rebuilt image, 491 entries). NonUI err=2 is expected (not in donor). Only bindfs line is the FAILED bind of /LaunchDaemons onto /flash/System/Library/LaunchDaemons (err 2, source missing in ramdisk) — no successful bindfs shadowed the dir. /System/Library/LaunchDaemons shows as a directory (drwxr-xr-x), not a symlink, in the listing. No import lines for backboardd/SpringBoard (only 3 error-152 imports from DeveloperModeLaunchDaemons). 
- INTERPRETATION: launchd opened the dir but never imported backboardd/SpringBoard; the D22 err=2 strongly suggests launchd is seeing a DIFFERENT /System/Library/LaunchDaemons than the flat root's donor dir (most likely the ramdisk's symlink target /redwood/LaunchDaemons, which only has varstore.plist) — possibly a root-pivot/bindfs ordering issue in init.c flash_root, or launchd enumerated the dir before the pivot. Next: verify via console what /System/Library/LaunchDaemons resolves to in the booted system (ls it, count entries) and inspect init.c flash_root bindfs/pivot ordering.

## 2026-11-08 ~15:10-15:25 INVESTIGATION — why launchd does NOT import donor LaunchDaemons (ROOT CAUSE FOUND, init.c bindfs by design)
- Console `ls` check NOT possible: phone runs Apple's launchd as PID 1 (no HakiOS interactive shell); console only accepts kernel commands. `ls` commands were echoed but produced zero output. Evidence therefore from log + image + code (conclusive).
- Image VERIFIED correct: list_apfs.py on new flat.apfs — /System/Library/LaunchDaemons has 491 entries incl. com.apple.backboardd.plist, com.apple.SpringBoard.plist, com.apple.jetsamproperties.D22.plist. Image is NOT the problem.
- ROOT CAUSE (CONFIRMED): init.c flash_root() line 196 DELIBERATELY bind-mounts the ramdisk's /LaunchDaemons (varstore only) over /flash/System/Library/LaunchDaemons. Comment init.c:123-124: "our choice of launchd jobs instead of Darwin's 500 (this image's LaunchDaemons...)". The bind SUCCEEDED on the first flash_root run (gui log line 283: "[ ok ] our launchd jobs, not Darwin's"), BEFORE the pivot. After vfs_switch_root (/flash becomes /), /System/Library/LaunchDaemons = the ramdisk's varstore-only dir — the donor's 491 plists are HIDDEN by the bind. Launchd opens the dir (fd=4, err=0), sees only varstore.plist, spawns only varstore; D22.plist -> ENOENT (err=2). Ramdisk root/LaunchDaemons/ contains ONLY com.hakios.varstore.plist (verified).
- The second flash_root run's bind FAILURE (gui log lines 729-731: "bindfs /LaunchDaemons on /flash/System/Library/LaunchDaemons: No such file or directory", "[FAIL] our launchd jobs, not Darwin's") is a RED HERRING: it happens after the pivot, when /LaunchDaemons does not exist in the flat root — but the FIRST bind already persists across the pivot (mounts are preserved by vfs_switch_root), so the donor dir stays hidden. flash_root runs twice because the init re-execs (log lines 267/719 both "init: process 1, started as /sbin/launchd"; /etc/rc exec is re-run).
- varstore is SAFE if the bind is removed: launchd ALSO opens /redwood/LaunchDaemons separately (fd=4, err=0), and com.hakios.varstore.plist lives there — varstore will still load.
- NEXT STEP (proposed, NOT done — no code edited): remove the `bind_dir(in_image("/LaunchDaemons"), "/flash/System/Library/LaunchDaemons")` term from the check() at init.c lines 196-199 (KEEP the NanoLaunchDaemons, LaunchAngels, xpc binds). Then rebuild userland root.apfs, re-boot with root_apfs=ufs_darwin, check backboardd/SpringBoard spawn. Caveat: launchd will then try to load all 491 donor daemons; many will fail (missing hardware/services) but that is expected/non-fatal. Alternatives: (B) merge donor plists into ramdisk /LaunchDaemons (more invasive, ramdisk is 4 MB); (C) make the bind conditional on a boot-arg. Phone left alive on flat root v2, idle, /dev/ttyACM0 present; no reboot/flash/build/code edit done in this investigation.

## 2026-11-08 ~15:16-15:45 INIT.C BIND FIX — PASS: backboardd (pid 40) + SpringBoard (pid 9) SPAWNED AND RAN; then watchdogd PANIC (new problem)
- ROOT CAUSE FIX: removed exactly 1 term from init.c:196 — `bind_dir(in_image("/LaunchDaemons"), "/flash/System/Library/LaunchDaemons")` — from the check() in flash_root() (lines 196-199). KEPT the other 3 binds (NanoLaunchDaemons, LaunchAngels, xpc). init.c md5 ab8d6436 -> f2f5690d. Verified: grep LaunchDaemons shows only comments + NanoLaunchDaemons bind; the standalone /LaunchDaemons bind is gone; C syntax intact (check() still balanced).
- BLOCKER DURING BUILD: `./build.sh userland` first FAILED (BUILD-EXIT:1) — integrity check: upstream/hakios-uefi HEAD was 687fa6c, expected 1c65228 (UEFI switch updated build.sh's pin but not the upstream mirror). FIXED by fetching + checking out 1c65228 in upstream/hakios-uefi (updating a checkout, not editing; repo clean). Re-ran `./build.sh userland` -> BUILD-EXIT:0, root.apfs 4194304 B, md5 d8156371 (old 559d580e).
- SECOND BLOCKER: the rebuilt root.apfs was MISSING /etc/rc (init dropped to interactive shell, PICASSO-DIAG: open /redwood/etc/rc -> err=2). Root cause: userland/build.sh line 458 `cp ${RC:-rc.launchd} root/etc/rc` did not produce root/etc/rc in this build (RC was empty, rc.launchd exists with `exec /sbin/launchd.darwin`, yet root/etc/ had only motd after the build — cause of the missing cp not determined). FIX: manually `cp rc.launchd root/etc/rc`, then rebuilt root.apfs via mkapfs.py directly (redwood=root, 14 files, 885 free). New root.apfs md5 6c6458cb (verified /etc/rc present via list_apfs.py). Copied to build/out/root.apfs too. NOTE: future full `./build.sh userland` runs may drop /etc/rc again — verify root/etc/rc exists after any userland rebuild.
- REBOOT: console `reboot-fastboot` via pyserial (first attempt's SSH session hung ~8 min and was killed before the command was confirmed sent; second attempt SENT-OK). Phone to xnuloader fastboot (~50 s).
- BOOT v3b (run-log build/flatroot-v3b-run.log, boot 15:36, kernel 1368bbc4, HAKIOS_EXTRA_ARGS='wifi=0 adreno=0 gpu=0 root_apfs=ufs_darwin'): gui log FRESH (mtime 15:40, 1,127,374 B, **11886 lines** vs v2's 4972). Flat root v2 mounted (47885 files).
- **PASS — backboardd + SpringBoard SPAWNED AND RAN:**
  - backboardd: xpcproxy spawned pid 40 (00:01:53.509), service state running, "Successfully spawned backboardd[40] because speculative" (00:01:54.762). Still active at 00:04:49 creating service stubs (CallKit, StreamingUnzip, FTLivePhoto, FaceTime).
  - SpringBoard: "Successfully spawned SpringBoard[9] because system support" (00:01:53.606), SOURCE_ATTACH, "uncorking exec source upfront", job state = running at 00:04:19.
  - D22.plist now opens err=0 (was err=2 in v2): `PICASSO-DIAG: PID 1 open: /System/Library/LaunchDaemons/com.apple.jetsamproperties.D22.plist -> err=0 fd=3` — launchd CAN see the donor LaunchDaemons now.
  - 122 services "Successfully spawned" (vs 1 in v2 — varstore). xpc.launchd 3976 lines.
  - NO reboot(RB_HALT) from PID 1 (grep reboot/RB_HALT = 0).
- **NEW PROBLEM — phone PANICKED after ~3 min, now in fastboot:** log line 11859: `Panicked task 0xfffffff58069de10: 73 pages, 2 threads: pid 130: watchdogd`. Panicked thread tid 692, CORE 7 PC=0x0 LR=0x0 FP=0x0. Stackshot Succeeded (96226 bytes traced). "Please go to https://panic.apple.com to report this panic". `=== PICASSO CDC PANIC FLUSH COMPLETE: FASTBOOT REASON ARMED ===`. Phone in xnuloader fastboot; /dev/ttyACM0 gone. The panic is from watchdogd (pid 130), NOT from backboardd/SpringBoard (both were running fine until the panic). Separate problem from the bind fix.
- **SIDE NOTE — varstore did NOT spawn in this boot:** zero var_store spawn/running lines (grep "var_store" + spawn|running|Successfully|EXITED = 0). In v2 varstore spawned (pid 7). Now launchd loads 491 donor jobs; varstore (in /redwood/LaunchDaemons/) was not imported/spawned. Unclear if varstore is on-demand-only or was skipped; needs investigation (it manages /private/var). Not blocking the backboardd/SpringBoard result, but flag it.
- Phone now in xnuloader fastboot. k60 root.apfs md5 6c6458cb (with /etc/rc + fixed init). No build.sh edit, no export-patches, no commit/push, no kernel build, no UFS flash in this task.

## 2026-11-08 ~15:42-15:55 — watchdogd panic: ROOT CAUSE (investigation)
- v3b boot: watchdogd crashes SIGTRAP 3x; launchd panic-on-crash turns 3rd crash into kernel panic.
- pid 35 spawn 00:01:53, crash SIGTRAP 00:02:08 (line 7758); pid 80 crash 00:03:43 (line 9118); pid 130 spawn 00:03:45, "Enabling panic-on-crash" 00:03:46, crash ~00:05:08 -> kernel panic (line 11859).
- Core dump failed (/private/var/cores missing, ENOENT). tccd also SIGTRAP 3x but does NOT panic kernel.
- Other exits: many exit(78) EX_CONFIG (expected); exit(0) clean; exit(1) biomed/accessoryupdaterd/iomfb_fdr_loader/analyticsd; SIGABRT clusters (dmd+ScreenTimeAgent) suggest common trigger, not identified.
- varstore NOT loaded in v3b: launchd does not auto-load /redwood/LaunchDaemons (not a standard job dir); v2 only spawned it via the (removed) bind. varstore absence is NOT the panic cause, but /private/var won't persist.

## 2026-11-08 ~15:51-16:05 — flat.apfs v3 (watchdogd removed, varstore added) — BUILT, NOT FLASHED
- Sysroot: removed com.apple.watchdogd.plist, added com.hakios.varstore.plist (491 files). Rebuild MKAPFS-EXIT:0, 47885 files, 3,489,058,816 B, md5 9c61c96809af4a7f702b3611b34101e0, sha256 59d386cf..., dated 15:58.
- Re-split: 5 pieces (sparse-split default ~768 MB max), piece-00..04, all under ABL 805,306,368 B. Stale v2 pieces 05..53 deleted. pieces/ now has only 5 v3 pieces.
- v3 NOT flashed (superseded by ramdisk fix below). Darwin still has flat.apfs v2.

## 2026-11-08 ~16:13-16:21 — RAMDISK-ONLY watchdogd fix — PASS (no UFS write)
- User: "Tôi không muốn debug mà phải ghi vào ufs". Approach: curated LaunchDaemons in ramdisk, bind re-enabled, UFS flat root v2 untouched.
- LESSON: ./build.sh userland WIPES root/LaunchDaemons/ (mkjobs.py only populates if $DARWIN_JOBS set + $DARWIN/rootfs exists; donor rootfs GONE from k60, so only varstore survives) and can LOSE root/etc/rc. Fix: AFTER build.sh, manually copy 490 donor plists (minus watchdogd) + varstore into root/LaunchDaemons/ (491 files) and rc.launchd -> root/etc/rc, then rebuild root.apfs DIRECTLY with mkapfs.py.
- init.c bind RE-ENABLED (md5 back to ab8d6436, original). New root.apfs md5 a1414c2997c2cb7df8a296a0d2a1e4cf (4 MB, 504 files), verified 491 plists (backboardd+varstore present, watchdogd ABSENT), /etc/rc present.
- Boot ramdisk-v4 (16:17, kernel 1368bbc4, root_apfs=ufs_darwin, NO flash): PASS. Log FRESH 1,115,910 B, 12217 lines, mtime 16:21.
- watchdogd NEVER spawns (only 2 benign file-listing mentions of binary). NO kernel panic (0 "Panicked task"). NO watchdogd SIGTRAP. Phone alive (/dev/ttyACM0, fastboot EMPTY) past 4 min (v3b panicked ~3 min).
- backboardd pid 40 + SpringBoard pid 9 spawned. varstore pid 35 spawned, ran, exit(0). Bind "[ ok ]" line 286.
- UFS flat.apfs v3 now UNNEEDED for watchdogd fix. Next: watch physical screen for GUI (SpringBoard/backboardd running; no GUI seen yet).

## 2026-11-08 ~16:26-17:00 — NO GUI: 0 swaps; var.skel + GPU fixed, presentation still blocked
- User: "Chua co giao dien nhe". Phone ALIVE, backboardd pid 40 + SpringBoard pid 9 spawned, 2 framebuffer user clients connected, IOSurface opened. But grep "MiniIOMobileFramebuffer: swap" = 0 — backboardd NEVER presented a frame. "graphics mode enabled" absent.
- fb_beam NOT the cause (tear-only, frames still written on beat). fb_intf boot-arg for DPU INTF regs (default sc7280 0x0ae36000; picasso DSI0 @ 0xae94000).
- ROOT CAUSE of missing /private/var: /etc/var.skel did NOT exist in ramdisk (build.sh:417 cp $DARWIN/var.skel fails silently; donor rootfs GONE from k60). mount_writable read empty skeleton -> "0 directories, 0 files" + [FAIL] -> /private/var empty tmpfs -> 60 "No such file" errors (/tmp, /var/run missing).
- Fix (ramdisk-only): hand-wrote /etc/var.skel (99 dirs + 1 symlink), uploaded via scp -O, rebuilt root.apfs with mkapfs.py. LESSON: literal TABs in SSH heredoc get MANGLED by PTY — write tab-separated files locally, upload via scp -O.
- Boot ramdisk-v5 (16:49, root.apfs 2f17efdb): 81 dirs, errors 60->25. Boot ramdisk-v6 (16:53, GPU ON adreno=22 gpu=1, root.apfs 2a5e915f): 92 dirs, GPU "THE GPU STAYS ON... HakiosGPU registered for Mesa/freedreno". BUT swap STILL 0. GPU hypothesis RULED OUT.
- var.skel [FAIL] persists due to DUPLICATE entries in hand-written file (lockdown x2, timed x2 — fixed locally, not yet re-uploaded; cosmetic).
- No panic in either boot. Next: root cause of 0 swaps still unknown — backboardd connects but never calls SwapEnd; likely display-recognition or backboardd/SpringBoard startup issue; consider ios-redfin display comparison.

## 2026-11-08 ~17:09-17:30 — ios-redfin comparison + TWO kernel fixes, BOTH RULED OUT
- KEY EVIDENCE: backboardd NEVER calls ANY framebuffer method. Only MobileGestaltHelper[83] calls GetDisplaySize (sel 8) + GetDotPitch (sel 28). 0 SwapBegin (sel 4), 0 SwapEnd (sel 5), 0 Notifications (sel 72). 2 user clients connected; 2nd client unknown (backboardd may not have opened one at all).
- Subagent research (ios-redfin origin/ws-u-display): redfin uses AppleCLCD/SweetFramebuffer (full upstream driver), same SwapEnd protocol (0x46c-byte record, sel 4/5/6). Redfin patch 0121 proved vsync-period=0 makes CA see every frame deadline as past -> no frames. Redfin sets "external"=false in start().
- Fix #1 (vsync period): MiniIOMobileFramebuffer.cpp vsyncFired sent values[4-5]=0,0 for the refresh period. FIXED: period via nanoseconds_to_absolutetime(NSEC_PER_SEC/refreshHz()). Kernel ffa351dd, boot vsync-fix (17:16): STILL 0 swaps, 0 SwapEnd, 0 vsync-sent, 0 notify-sub.
- Fix #2 (external=false): picasso never setProperty("external", false). ADDED setProperty("external", kOSBooleanFalse) in start(). Kernel 434306ba, boot vsync-ext (17:26): STILL 0 swaps, 0 SwapEnd, 0 notify-sub after 4 min (log 1.2 MB).
- CONCLUSION: backboardd is stuck BEFORE it subscribes to vsync / sets up its render loop. Neither the vsync period nor the external property is the (only) blocker. Both fixes KEPT in kernel (correct per redfin) but do not unblock presentation. Next: investigate why backboardd does not find/use disp0 or what it waits on (SpringBoard XPC? display-connected event? IOKit matching?).

## 2026-11-08 ~17:32-17:40 — DIAGNOSTIC: backboardd NEVER opens the framebuffer
- Added PICASSO-DIAG to newUserClient (logs process name + pid via proc_selfname/proc_selfpid). Kernel b974df5b, boot diag-client (17:35).
- Result: the ONLY newUserClient call is `by IOMFB_FDR_Loader[72]` (a diagnostic loader). backboardd[40] is ALIVE (job state=running, creating XPC service stubs for CallKit/StreamingUnzip/FTLivePhoto) but NEVER attempts to open the framebuffer — no newUserClient call from it, no failure lines. SpringBoard also running (38 mentions). SWAP still 0.
- CONCLUSION: backboardd is stuck in startup BEFORE display initialization — it does XPC service setup but never gets to finding/opening disp0. This is NOT a vsync/framebuffer-protocol problem; it is a backboardd startup/display-discovery problem. Next: why does backboardd not look for displays? (Missing Mach service? SpringBoard XPC handshake? IOKit matching? Display-connected notification?)
