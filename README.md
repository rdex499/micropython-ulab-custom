# MicroPython + ulab Custom Build for Raspberry Pi Pico

This repository contains a complete build environment for MicroPython with custom ulab modifications for the Raspberry Pi Pico 2 W.

## Custom Modifications

- **STFT Implementation**: Added `scipy.signal.stft()` for time-frequency analysis of audio signals

## What's Inside

- **micropython/**: MicroPython source code and build system
- **ulab/**: Custom ulab module with modifications

## Building Firmware

### Prerequisites
- CMake
- ARM GCC toolchain
- Make

### Build Steps

1. Clone this repository:
```bash
git clone https://github.com/rdex499/micropython-ulab-custom.git
cd micropython-ulab-custom
```

2. Compile mpy-cross:
```bash
cd micropython/mpy-cross
make
cd ../..
```

3. Initialize submodules:
```bash
cd micropython/ports/rp2
make BOARD=RPI_PICO2_W submodules
```

4. Build firmware:
```bash
make BOARD=RPI_PICO2_W USER_C_MODULES=../../../ulab/code/micropython.cmake
```

5. The firmware will be in `micropython/ports/rp2/build-RPI_PICO2_W/firmware.uf2`

### Building for Other Boards

Replace `RPI_PICO2_W` with:
- `RPI_PICO` - Original Raspberry Pi Pico (RP2040)
- `RPI_PICO_W` - Pico W with WiFi (RP2040)
- `RPI_PICO2` - Pico 2 (RP2350)

## Flashing to Pico

1. Hold BOOTSEL button on Pico
2. Plug in USB cable
3. Release BOOTSEL - Pico appears as USB drive
4. Drag firmware.uf2 onto the drive
5. Pico reboots with new firmware

## Configuration

STFT can be enabled/disabled in `ulab/code/ulab.h`:
```c
#ifndef ULAB_SCIPY_SIGNAL_HAS_STFT
#define ULAB_SCIPY_SIGNAL_HAS_STFT          (1)
#endif
```

## License

- MicroPython: MIT License
- ulab: MIT License
