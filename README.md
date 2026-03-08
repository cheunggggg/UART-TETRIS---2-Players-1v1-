# UART Tetris 2P

> Created as a final project for COM SCI 152B at UCLA by Brandon Cheung and Rahul Mohan.

Two-player simultaneous Tetris running on a **Digilent Basys 3** via a MicroBlaze soft-core processor. Both boards render side-by-side in a single PuTTY terminal over USB-UART using VT100 ANSI escape codes. Supports four input device types with dynamic controller assignment. No recompilation needed!

```
PLAYER 1              PLAYER 2
Score:0               Score:200
+--------------------+  +--------------------+
|                    |  |                    |
|                    |  |    [][]            |
|                    |  |  [][]              |
|        []          |  |                    |
|      [][][]        |  |                []  |
|                    |  |              [][][]|
+--------------------+  +--------------------+
```

---

## Hardware

| Component | Details |
|-----------|---------|
| FPGA Board | Digilent Basys 3 |
| Processor | MicroBlaze soft-core, 100 MHz |
| EDA Tool | Vivado 2018.2 + Xilinx SDK |
| Display | PuTTY terminal via USB-UART at 9600 baud |

### AXI Peripherals (Block Design)

| Peripheral | Role |
|------------|------|
| AXI UARTLite | USB-UART to PC (wired keyboard + all terminal output) |
| PmodBT2 on PMOD JB | RN-42 Bluetooth module (XUartNs550 driver) |
| PmodKYPD on PMOD JA | 16-key 4×4 matrix keypad |
| AXI GPIO ch1 | Push buttons: BTNU/L/R/D |
| AXI GPIO ch2 | DIP switches SW0–SW1 |

> **Note:** BTNC is hardwired to the CPU reset net and is not available as GPIO.

---

## Input Devices

Four input sources are supported simultaneously. The first two distinct devices to send input are automatically assigned as Player 1 and Player 2.

### Wired Keyboard (AXI UARTLite)
USB keyboard → PC → PuTTY → FPGA over UART at 9600 baud.

| Key | Action |
|-----|--------|
| `R` | Rotate |
| `A` | Move Left |
| `D` | Move Right |
| `S` | Soft Drop |
| `E` | Hard Drop |
| `P` | Continue / Return Home |

### Bluetooth Keyboard (PmodBT2 / RN-42)
Pair the module as **TetrisBT** (PIN: `1234`). It creates a virtual Bluetooth COM port on Windows. Open a second PuTTY session on that COM port at 9600 baud — keystrokes are forwarded to the FPGA via XUartNs550. Same key bindings as the wired keyboard.

> The RN-42 operates in **SPP mode** (not HID). Any keyboard input through the paired PuTTY window works as a controller.

### PmodKYPD 16-Key Keypad (PMOD JA)
Keytable: `0FED789C456B123A`

| Key | Action |
|-----|--------|
| `A` | Rotate |
| `5` | Move Left |
| `B` | Move Right |
| `8` | Soft Drop |
| `F` | Hard Drop |
| `2` | Continue / Return Home |

### Basys3 Push Buttons + DIP Switches (AXI GPIO)
Bit mapping verified via hardware test program.

| Input | Action |
|-------|--------|
| BTNU (bit 0) | Rotate |
| BTNL (bit 1) | Move Left |
| BTNR (bit 2) | Move Right |
| BTND (bit 3) | Hard Drop |
| SW1 (flip ON) | Soft Drop — one drop per rising edge |
| SW0 (flip ON) | Continue / Return Home — rising edge triggered |

---

## Game Features

- 7 standard tetrominoes with wall-kick rotation (tries ±1 and ±2 column offsets)
- Gravity, soft drop, and hard drop
- Line clear with scoring: **100 points per line**
- **Garbage row mechanic:** clearing 2+ lines sends `(cleared - 1)` penalty rows to the opponent, each with a random hole
- **Progressive difficulty:** frame delay steps down from 400 ms toward 80 ms minimum every ~30 seconds
- Game over when a new piece cannot spawn (board topped out)
- Winner determined by score if both players die simultaneously; otherwise the surviving player wins

---

## Game Flow

```
Boot
 └─► Connect Screen     (poll all devices; first two to register become P1/P2)
      └─► Home Menu      (shows per-player controls based on assigned devices)
           └─► Gameplay  (simultaneous 2P Tetris)
                └─► Game Over Screen  (winner + scores)
                     └─► Home Menu    (loop)
```

---

## Terminal Rendering

All output uses VT100 ANSI escape codes over the single UARTLite connection:

- `\033[2J` — clear screen
- `\033[H` — reset cursor to home

At 9600 baud a full two-board frame takes ~1 second, which sets the effective frame rate to ~1 FPS. All timing constants (gravity tick, speed interval, frame delay) are calibrated to this.

---

## Files

| File | Description |
|------|-------------|
| `TETRIS.c` | Full game source — peripheral I/O, device abstraction, game logic, rendering |
| `constraints.xdc` | Vivado XDC pin constraints for Basys 3 |

### Key `TETRIS.c` Constants

```c
#define WIDTH              10
#define HEIGHT             20
#define GRAVITY_TICKS       1    // rows fall 1 tick per frame
#define FRAME_DELAY_START 400    // starting frame delay (µs)
#define FRAME_DELAY_MIN    80    // fastest frame delay (µs)
#define SPEED_INTERVAL     30    // frames between speed increases
#define DEFAULT_KEYTABLE  "0FED789C456B123A"  // KYPD decode table
```

### XDC Pin Summary

| Signal | Package Pin | Notes |
|--------|-------------|-------|
| `sys_clock` | W5 | 100 MHz, 10 ns period |
| `reset` | U17 | Active high |
| `usb_uart_rxd` | B18 | Wired keyboard / terminal RX |
| `usb_uart_txd` | A18 | Terminal TX |
| `jb[0:7]` | A14/A16/B15/B16/A15/A17/C15/C16 | PMOD JB → PmodBT2 |
| `ja[0:7]` | B13/F14/D17/E17/G13/C17/D18/E18 | PMOD JA → PmodKYPD |
| `push_buttons_4bits_tri_i[0:4]` | U18/T18/W19/T17/W17 | BTNU/L/R/D + extra |
| `dip_switches_16bits_tri_i[0:1]` | V17/V16 | SW0/SW1 |

---

## Build & Deploy

1. Open Vivado 2018.2 and recreate the block design with the peripherals listed above.
2. Apply `constraints.xdc` to your project.
3. Generate the bitstream and export hardware to Xilinx SDK.
4. In SDK, create a new C application project and add `TETRIS.c` as the source.
5. Program the FPGA and run the application.
6. Open PuTTY: **Serial**, the board's COM port, **9600 baud**, with **implicit CR** enabled (`Terminal → Implicit CR in every LF`).
7. To use BT2: pair `TetrisBT` on Windows (PIN `1234`), then open a second PuTTY session on the Bluetooth COM port at 9600 baud.

---

## Known Limitations

- **~1 FPS** — UART rendering at 9600 baud takes ~1 second per frame
- **Single terminal output** — both players share one PuTTY display
- **BTNC unusable** — hardwired to CPU reset, only 4 buttons available for gameplay
- **BT2 SPP mode only** — Bluetooth input requires a PuTTY relay, not direct HID
- **Fixed random seed** (`srand(1)`) — piece sequence is deterministic per session
