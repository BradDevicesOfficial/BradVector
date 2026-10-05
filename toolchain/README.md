# Brad Devices — Reference Implementations

Reference C headers, driver stubs, and build infrastructure for the Brad Devices hardware ecosystem.

## Directory Structure

```
src/
├── CMakeLists.txt              # Top-level build
├── README.md                   
├── include/
│   └── brad/
│       ├── hypercore_atlas.h    # HyperCore/Atlas register definitions
│       ├── bradcore.h           # BradCore CPU register definitions
│       ├── bradfx.h             # BradFx integrated GPU definitions
│       ├── bradgfx.h            # BradGfx dedicated GPU definitions
│       ├── bradnpu.h            # BradNPU neural processor definitions
│       ├── fabric.h             # BradFusion Fabric interconnect API
│       ├── spmp.h               # Super Power Memory Pool API
│       ├── bradram.h            # BradRAM die interface
│       ├── braddrive.h          # BradDrive storage interface
│       ├── bradcompress.h       # HDB compression/decompression API
│       ├── brados.h             # BradOS process management
│       ├── eroe.h               # Eco-Render Optimization Engine API
│       ├── bradlink.h           # BradLink device tethering API
│       ├── bradsec.h            # BradSec security API
│       ├── bradcvt.h            # Brad-CVT CUDA translator API
│       └── tifa.h               # TIFA AI SDK API
├── drivers/
│   ├── CMakeLists.txt
│   ├── hypercore_atlas.c        # HyperCore driver stub
│   ├── bradnpu.c                # NPU driver stub
│   ├── eroe.c                   # EROE power governor stub
│   ├── bradlink.c               # BradLink driver stub
│   ├── fabric.c                 # Fabric driver stub
│   ├── spmp.c                   # SPMP memory manager stub
│   └── bradcvt_stub.c           # Brad-CVT translator stub
├── brad-cvt/                    # Brad-CVT toolchain (future)
└── tests/
    ├── CMakeLists.txt
    ├── test_eroe.c
    ├── test_hypercore.c
    ├── test_npu.c
    └── test_fabric.c
```

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/opt/brad
make
make test
```

## Options

| Flag | Default | Description |
|------|---------|-------------|
| `BRAD_BUILD_DRIVERS` | ON | Build driver stubs |
| `BRAD_BUILD_EMU` | OFF | Build emulation layer |
| `BRAD_BUILD_CVT` | OFF | Build Brad-CVT toolchain |
| `BRAD_BUILD_TESTS` | ON | Build unit tests |

## Requirements

- C23-capable compiler (GCC 15+, Clang 18+)
- CMake 3.28+
- Linux (for full Fabric/Fabric simulation)
