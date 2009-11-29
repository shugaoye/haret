// Support for Qualcomm MSMxxxx cpus.
#include "script.h" // runMemScript
#include "arch-arm.h" // cpuFlushCache_arm6
#include "arch-msm.h"

static void
defineMsmGpios()
{
    runMemScript(
        // out registers?
        "addlist gpios p2v(0xa9200800)\n"
        "addlist gpios p2v(0xa9300c00)\n"
        "addlist gpios p2v(0xa9200804)\n"
        "addlist gpios p2v(0xa9200808)\n"
        "addlist gpios p2v(0xa920080c)\n"
        // in registers?
        "addlist gpios p2v(0xa9200834)\n"
        "addlist gpios p2v(0xa9300c20)\n"
        "addlist gpios p2v(0xa9200838)\n"
        "addlist gpios p2v(0xa920083c)\n"
        "addlist gpios p2v(0xa9200840)\n"
        // out enable registers?
        "addlist gpios p2v(0xa9200810)\n"
        "addlist gpios p2v(0xa9300c08)\n"
        "addlist gpios p2v(0xa9200814)\n"
        "addlist gpios p2v(0xa9200818)\n"
        "addlist gpios p2v(0xa920081c)\n"
        );
}

static void
defineQsdGpios()
{
    // QSD8xxx has eight GPIO banks (0-7)
    // GPIO1 base: 0xA9000000 + 0x800
    // GPIO2 base: 0xA9100000 + 0xc00
    runMemScript(
        // output registers
        "addlist gpios p2v(0xa9000800)\n"
        "addlist gpios p2v(0xa9100c00)\n"
        "addlist gpios p2v(0xa9000804)\n"
        "addlist gpios p2v(0xa9000808)\n"
        "addlist gpios p2v(0xa900080c)\n"
        "addlist gpios p2v(0xa9000810)\n"
        "addlist gpios p2v(0xa9000814)\n"
        "addlist gpios p2v(0xa9000818)\n"
        // input read registers
        "addlist gpios p2v(0xa9000850)\n"
        "addlist gpios p2v(0xa9100c20)\n"
        "addlist gpios p2v(0xa9000854)\n"
        "addlist gpios p2v(0xa9000858)\n"
        "addlist gpios p2v(0xa900085c)\n"
        "addlist gpios p2v(0xa9000860)\n"
        "addlist gpios p2v(0xa9000864)\n"
        "addlist gpios p2v(0xa900086c)\n"
        // output enable registers
        "addlist gpios p2v(0xa9000820)\n"
        "addlist gpios p2v(0xa9100c08)\n"
        "addlist gpios p2v(0xa9000824)\n"
        "addlist gpios p2v(0xa9000828)\n"
        "addlist gpios p2v(0xa900082c)\n"
        "addlist gpios p2v(0xa9000830)\n"
        "addlist gpios p2v(0xa9000834)\n"
        "addlist gpios p2v(0xa9000838)\n"
        );
}


/****************************************************************
 * MSM 7xxxA
 ****************************************************************/

MachineMSM7xxxA::MachineMSM7xxxA()
{
    name = "Generic MSM7xxxA";
    flushCache = cpuFlushCache_arm6;
    arm6mmu = 1;
    archname = "MSM7xxxA";
    CPUInfo[0] = L"MSM7201A";
}

void
MachineMSM7xxxA::init()
{
    runMemScript(
        "set ramaddr 0x10000000\n"
        "addlist irqs p2v(0xc0000080) 0x100 32 0\n"
        "addlist irqs p2v(0xc0000084) 0 32 0\n"
        );
    defineMsmGpios();
}

REGMACHINE(MachineMSM7xxxA)


/****************************************************************
 * MSM 7xxx
 ****************************************************************/

MachineMSM7xxx::MachineMSM7xxx()
{
    name = "Generic MSM7xxx";
    flushCache = cpuFlushCache_arm6;
    arm6mmu = 1;
    archname = "MSM7xxx";
    CPUInfo[0] = L"MSM7500";
    CPUInfo[1] = L"MSM7200";
}

void
MachineMSM7xxx::init()
{
    runMemScript(
        "set ramaddr 0x10000000\n"
        "addlist irqs p2v(0xc0000000) 0x100 32 0\n"
        "addlist irqs p2v(0xc0000004) 0 32 0\n"
        );
    defineMsmGpios();
}

REGMACHINE(MachineMSM7xxx)

/****************************************************************
 * QSD 8xxx
 ****************************************************************/

MachineQSD8xxx::MachineQSD8xxx()
{
    name = "Generic QSD8xxx";
    flushCache = cpuFlushCache_arm7;
    arm6mmu = 1;
    archname = "QSD8xxx";
    CPUInfo[0] = L"QSD8250B"; // First seen on HTC Leo
    CPUInfo[1] = L"QSD8250"; // First seen on Acer S200 (F1)
}

void
MachineQSD8xxx::init()
{
    runMemScript(
        "set ramaddr 0x18800000\n"
        "addlist irqs p2v(0xac000080) 0x100 32 0\n" // 0x100 masks out the 9th interrupt (DEBUG_TIMER_EXP)
        "addlist irqs p2v(0xac000084) 0 32 0\n"
    );
    defineQsdGpios();
}

REGMACHINE(MachineQSD8xxx)
