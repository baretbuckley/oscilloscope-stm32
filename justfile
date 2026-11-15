# build, flash and debug the scope. pyocd reads its pack and target from
# pyocd.yaml, so run these from the repo root

mode := "Debug"
elf := "build/" + mode + "/oscilloscope-stm32.elf"
pack_url := "https://github.com/Open-CMSIS-Pack/STM32H5xx_DFP/releases/download/v2.3.1/Keil.STM32H5xx_DFP.2.3.1.pack"

_default:
    @just --list

# configure and build, the elf lands in build/<mode>/
build mode=mode:
    cmake --preset {{ mode }}
    cmake --build build/{{ mode }}

# the H5 device pack for pyocd, downloaded once
pack:
    test -e .pyocd/Keil.STM32H5xx_DFP.2.3.1.pack || curl -fL --create-dirs -o .pyocd/Keil.STM32H5xx_DFP.2.3.1.pack {{ pack_url }}

# build and flash, then the board runs it
flash mode=mode: (build mode) pack
    pyocd flash build/{{ mode }}/oscilloscope-stm32.elf

# flash and open gf2 halted at reset, with a breakpoint in shell_grabbed so
# every grab stops there. press continue in gf2 to start the board
debug: (build "Debug") pack
    #!/usr/bin/env bash
    set -euo pipefail
    pyocd gdbserver --persist >/tmp/pyocd-gdbserver.log 2>&1 &
    trap 'kill $!' EXIT
    sleep 1
    gf2 {{ elf }} -ex 'target extended-remote :3333' -ex load \
        -ex 'monitor reset halt' -ex 'break shell_grabbed'

# gdb server on :3333 for a debugger of your own
gdbserver: pack
    pyocd gdbserver --persist

# reset the board and let it run
reset: pack
    pyocd reset

# the board's virtual com port, the first /dev/ttyACM* unless one is given
serial port="" baud="115200":
    #!/usr/bin/env bash
    set -euo pipefail
    port={{ port }}
    if [ -z "$port" ]; then
        ports=(/dev/ttyACM*)
        [ -e "${ports[0]}" ] || { echo "no /dev/ttyACM*, is the board plugged in?" >&2; exit 1; }
        port=${ports[0]}
    fi
    exec tio -b {{ baud }} "$port"

# flash and ram used by a build
size mode=mode:
    arm-none-eabi-size build/{{ mode }}/oscilloscope-stm32.elf

clean:
    rm -r build
