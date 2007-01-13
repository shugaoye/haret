#include "arch-s3.h" // MachineS3
#include "mach-types.h"
#include "memory.h" // memPhysSize

class MachRX3715 : public MachineS3 {
public:
    MachRX3715() {
        name = "RX3715";
        OEMInfo[0] = L"HP iPAQ rx3700";
        machType = MACH_TYPE_RX3715;
    }
    void init() {
        MachineS3::init();
#if 0
        asic3_gpio_base=0x08000000;
        asic3_sdio_base=0x10000000;
        asic3_bus_shift=2;
#endif
        memPhysSize = 64*1024*1024;
    }
};

REGMACHINE(MachRX3715)
