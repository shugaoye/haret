// Definitions for Samsung s3c24xx chips.
#include "arch-arm.h" // Machine920t

class MachineS3c2442 : public Machine920t {
public:
    MachineS3c2442();
    void init();
    int preHardwareShutdown();
    void hardwareShutdown(struct fbinfo *fbi);

    uint32 *channels, *uhcmap;
};

// XXX - assume they are the same for now.
class MachineS3c2440 : public MachineS3c2442 {
};
class MachineS3c2410 : public MachineS3c2442 {
};
