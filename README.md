# AXI VDMA + HDMI Output Example — PYNQ-Z1

An example of using the **Xilinx AXI Video DMA (`axi_vdma`)** IP core on a
PYNQ-Z1 (Zynq-7020) together with a **Digilent `rgb2dvi`** HDMI transmitter to
build a capture → process → display video pipeline at 720p.

- A **Video Test Pattern Generator (`v_tpg`)** produces a test stream that is
  captured through the VDMA into DDR and exposed as a V4L2 device
  (`/dev/video0`).
- A C++ application (`tpg-overlay`) captures the video, overlays text with
  OpenCV, and writes the result to the HDMI framebuffer (`/dev/fb0`).
- The VDMA reads the framebuffer back from DDR and streams it through a video
  pipeline to the HDMI output.

---

## Overview

| Item        | Value                                                      |
|-------------|------------------------------------------------------------|
| Board       | PYNQ-Z1 (Zynq-7020, `xc7z020clg400-1`)                     |
| IP cores    | `axi_vdma` @ **0x4300_0000**, `v_tpg` @ **0x43C0_0000**, `v_tc` @ **0x43C1_0000** |
| GPIO        | `axi_gpio` @ **0x4120_0000** (HDMI) / **0x4121_0000** (switches/buttons) |
| HDMI         | `rgb2dvi` (Digilent IP) + `v_axi4s_vid_out` + `v_tc`, 720p    |
| Linux        | `/dev/video0` (V4L2 capture), `/dev/fb0` (HDMI framebuffer) |
| App          | `tpg-overlay` (C++20 + OpenCV + GStreamer)                   |
| Toolchain    | Vivado **2025.2**, Yocto **Scarthgap** (kas)                 |

## Setup environment

- Vivado 2025.2
- Yocto Scarthgap + [kas](https://kas.readthedocs.io/)
- PYNQ-Z1 board + HDMI monitor
- Ubuntu 22.04 / 24.04 (or a similar Linux host)

> The block design uses the Digilent `rgb2dvi` IP from the
> [`vivado-library`](https://github.com/Digilent/vivado-library) submodule.
> Clone with `git submodule update --init` before building.

## Hardware design

### Block design (`Vivado/scripts/pynq_z1_bd.tcl`)

The block design contains:

- `processing_system7` — the Zynq ARM Cortex-A9 PS (HP0 used by the VDMA).
- `axi_vdma_0` — AXI Video DMA (base address `0x4300_0000`):
  - **MM2S** (memory-mapped → stream) for the display path.
  - **S2MM** (stream → memory-mapped) for the capture path.
  - 32-bit MM data, 24-bit stream, 4 frame stores.
- `v_tpg_0` — Video Test Pattern Generator (base address `0x43C0_0000`).
- `v_tc_0` — Video Timing Controller (base address `0x43C1_0000`, inside the
  `hdmi_out_top` hierarchy) generating the 720p timing.
- `v_axi4s_vid_out_0` — AXI4-Stream → parallel video bridge.
- `rgb2dvi_0` — Digilent IP that serializes parallel RGB into TMDS
  (HDMI/DVI) lanes.
- `clk_hdmi_720p` — clock wizard generating the 720p pixel/serial clocks.
- `axi_gpio` × 2 — HDMI control (HPD/DDC, exposed as UIO) and the on-board
  switches/buttons.

### Data flow

```
          capture path
tpg ──▶ axi_vdma S2MM ──▶ DDR ──▶ /dev/video0 ──▶ app (OpenCV overlay)
                                                          │
          display path                                  ▼
HDMI ◀── rgb2dvi ◀── v_axi4s_vid_out ◀── axi_vdma MM2S ◀── DDR (/dev/fb0)
```

### Address map

| IP               | Base address |
|------------------|--------------|
| axi_vdma_0       | `0x4300_0000`|
| v_tpg_0          | `0x43C0_0000`|
| v_tc_0           | `0x43C1_0000`|
| axi_gpio (HDMI)  | `0x4120_0000`|
| axi_gpio (sw/btn)| `0x4121_0000`|

## Linux / device tree & kernel

`system-user.dtsi` sets up:

- A 32 MB reserved DMA pool (`vdma0_buffer @ 0x0100_0000`) for the framebuffer.
- `video_cap_tpg` — `xlnx,video` capture node bound to the VDMA S2MM channel
  (`dmas = <&axi_vdma_0 1>`), exposing `/dev/video0`.
- `xlnx_pl_disp` — `xlnx,pl-disp` display node bound to the VDMA MM2S channel
  (`dmas = <&axi_vdma_0 0>`), driving the HDMI output via the `digilent,hdmi`
  bridge and `v_tc` timing → `/dev/fb0`.
- `digilent_hdmi` — Digilent HDMI bridge (EDID over `i2c0`).
- `hdmi_out_top_axi_gpio_0` — exposed as `generic-uio` for HDMI control.

The kernel config (`linux-xlnx/xlnx-video.cfg`) enables the Xilinx video / DRM
subsystem drivers.

## Application (`apps/tpg-overlay`)

A C++20 app (`tpg-overlay` binary) that implements the capture → process →
display loop:

- **Capture** — GStreamer `v4l2src device=/dev/video0 ! … ! appsink` into an
  OpenCV `cv::VideoCapture`.
- **Overlay** — draws text (timestamp, TPG status) with OpenCV `cv::putText`.
- **Display** — GStreamer `appsrc ! … ! fbdevsink device=/dev/fb0` via OpenCV
  `cv::VideoWriter` → HDMI.
- `TpgDriver/` — configures the TPG through V4L2 controls (pattern, motion
  speed, box size, …).
- `Gpio/` — libgpiod wrapper for the switches/buttons.

Runtime controls:

| Control | Action                   |
|---------|--------------------------|
| SW1     | Increase motion speed    |
| SW2     | Decrease motion speed    |
| BTN1    | Cycle background pattern |
| BTN2    | Cycle foreground pattern |
| BTN3    | Increase box size        |
| BTN4    | Decrease box size        |

The binary is installed to `/home/root/tpg-overlay`.

## Build

### 1. Hardware — Vivado

```bash
git submodule update --init   # fetch Digilent vivado-library (rgb2dvi IP)
source <Vivado_install>/settings64.sh
cd Vivado
make
```

`make` runs the Tcl scripts to create the project, build the bitstream, and
export `pynq_z1_top.xsa` (plus `pynq_z1_top.bit`) at the `Vivado/` root.

### 2. Linux image — Yocto (kas)

Prepare the host for Yocto:

- [Preparing the build host](https://docs.yoctoproject.org/singleindex.html#preparing-the-build-host)
- [System requirements (Scarthgap)](https://docs.yoctoproject.org/scarthgap/ref-manual/system-requirements.html#system-requirements)

Install `kas`:

```bash
pip install kas
```

Build:

```bash
cd yocto
make
```

`make` copies `../Vivado/pynq_z1_top.xsa` into the HDF recipe and runs
`kas build build.yaml` (machine `zynq-generic-7z020`, target `image-pynq-z1`).
The image includes GStreamer, V4L2 tools, OpenCV, libdrm, libgpiod, the
`digilent-hdmi` module and the `tpg-overlay` app. The SD-card image is the
`.wic` file under `yocto/build/tmp-glibc/deploy/images/zynq-generic-7z020/`.

### 3. Flash & run

1. Flash the `.wic` image to a microSD card (e.g. with Balena Etcher).
2. Insert the card, connect the HDMI monitor, the USB UART (115200 8N1), and
   power the board.
3. Log in as `root` and run:

```bash
/home/root/tpg-overlay
```

The test pattern with text overlay should appear on the HDMI monitor; use the
switches/buttons to change the pattern at runtime.

## Repository layout

```
.
├── Vivado/                    # Hardware: block design, VHDL top, constraints
│   ├── scripts/               #   create_project.tcl, pynq_z1_bd.tcl, build_bitstream.tcl
│   ├── src/                   #   pynq_z1_top.vhd, pynq_z1.xdc
│   └── custom_ips/vivado-library/   # Digilent IP repo (submodule, rgb2dvi)
├── yocto/                     # Linux image: build.yaml + meta-user layer
│   └── meta-user/
│       ├── recipes-bsp/       #   device-tree, u-boot
│       ├── recipes-kernel/    #   kernel config (xlnx-video.cfg)
│       └── recipes-apps/      #   tpg-overlay
└── apps/
    └── tpg-overlay/           # C++20 capture/overlay/display app
```

## References

- [AXI Video Direct Memory Access — Product Guide (PG020)](https://docs.amd.com/r/en-US/pg020_axi_vdma)
- [Video Timing Controller — Product Guide (PG016)](https://docs.amd.com/r/en-US/pg016_v_tc)
- [Digilent vivado-library](https://github.com/Digilent/vivado-library)
- [PYNQ-Z1 Reference Manual](https://digilent.com/reference/programmable-logic/pynq-z1/reference-manual)
- [Xilinx DRM/KMS + V4L2](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842379/V4L2)