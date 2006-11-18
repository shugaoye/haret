/*
 * Linux loader for Windows CE
 *
 * (C) Copyright 2006 Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2003 Andrew Zabolotny
 *
 * This file may be distributed under the terms of the GNU GPL license.
 */


#include <stdio.h> // FILE, fopen, fseek, ftell
#include <ctype.h> // toupper

#define CONFIG_ACCEPT_GPL
#include "setup.h"

#include "xtypes.h"
#include "script.h" // REG_CMD
#include "util.h"  // fnprepare
#include "memory.h" // memPhysMap, memPhysAddr, memPhysSize
#include "output.h" // Output, Screen
#include "cpu.h" // take_control, return_control, touchAppPages
#include "video.h" // vidGetVRAM
#include "machines.h" // Mach

// Kernel file name
static char *bootKernel = "zimage";
// Initrd file name
static char *bootInitrd = "initrd";
// Kernel command line
static char *bootCmdline = "root=/dev/ram0 ro console=tty0";
// Milliseconds to sleep for nicer animation :-)
static uint32 bootSpeed = 5;
// ARM machine type (see linux/arch/arm/tools/mach-types)
static uint32 bootMachineType = 0;

REG_VAR_STR(0, "KERNEL", bootKernel, "Linux kernel file name")
REG_VAR_STR(0, "INITRD", bootInitrd, "Initial Ram Disk file name")
REG_VAR_STR(0, "CMDLINE", bootCmdline, "Kernel command line")
REG_VAR_INT(0, "BOOTSPD", bootSpeed
            , "Boot animation speed, usec/scanline (0-no delay)")
REG_VAR_INT(0, "MTYPE", bootMachineType
            , "ARM machine type (see linux/arch/arm/tools/mach-types)")

// Color codes useful when writing to framebuffer.
enum {
    COLOR_BLACK   = 0x0000,
    COLOR_WHITE   = 0xFFFF,
    COLOR_RED     = 0xf800,
    COLOR_GREEN   = 0x07e0,
    COLOR_BLUE    = 0x001f,
    COLOR_YELLOW  = COLOR_RED | COLOR_GREEN,
    COLOR_CYAN    = COLOR_GREEN | COLOR_BLUE,
    COLOR_MAGENTA = COLOR_RED | COLOR_BLUE,
};


/****************************************************************
 * Linux utility functions
 ****************************************************************/

// Recommended tags placement = RAM start + 256
#define PHYSOFFSET_TAGS   0x100
// Recommended kernel placement = RAM start + 32K
#define PHYSOFFSET_KERNEL 0x8000
// Initrd will be put at the address of kernel + 5MB
#define PHYSOFFSET_INITRD (PHYSOFFSET_KERNEL + 0x500000)
// Maximum size of the tags structure.
#define TAGSIZE (PAGE_SIZE - 0x100)

/* Set up kernel parameters. ARM/Linux kernel uses a series of tags,
 * every tag describe some aspect of the machine it is booting on.
 */
static void
setup_linux_params(char *tagaddr, uint32 phys_initrd_addr, uint32 initrd_size)
{
  struct tag *tag = (struct tag *)tagaddr;

  // Core tag
  tag->hdr.tag = ATAG_CORE;
  tag->hdr.size = tag_size (tag_core);
  tag->u.core.flags = 0;
  tag->u.core.pagesize = 0x00001000;
  tag->u.core.rootdev = 0x0000; // not used, use kernel cmdline for this
  tag = tag_next (tag);

  // now the cmdline tag
  tag->hdr.tag = ATAG_CMDLINE;
  // tag header, zero-terminated string and round size to 32-bit words
  tag->hdr.size = (sizeof (struct tag_header) + strlen (bootCmdline) + 1 + 3) >> 2;
  strcpy (tag->u.cmdline.cmdline, bootCmdline);
  tag = tag_next (tag);

  // now the mem32 tag
  tag->hdr.tag = ATAG_MEM;
  tag->hdr.size = tag_size (tag_mem32);
  tag->u.mem.start = memPhysAddr;
  tag->u.mem.size = memPhysSize;
  tag = tag_next (tag);

  /* and now the initrd tag */
  if (initrd_size)
  {
    tag->hdr.tag = ATAG_INITRD2;
    tag->hdr.size = tag_size (tag_initrd);
    tag->u.initrd.start = phys_initrd_addr;
    tag->u.initrd.size = initrd_size;
    tag = tag_next (tag);
  }

  // now the NULL tag
  tag->hdr.tag = ATAG_NONE;
  tag->hdr.size = 0;
}


/****************************************************************
 * Preloader
 ****************************************************************/

// Mark a function that is used in the C preloader.  Note all
// functions marked this way will be copied to physical ram for the
// preloading and are run with the MMU disabled.  These functions must
// be careful to not call functions that aren't also marked this way.
// They must also not use any global variables.
#define __preload __attribute__ ((__section__ (".text.preload")))

// Data Shared between normal haret code and C preload code.
struct preloadData {
    uint32 machtype;
    uint32 videoRam;
    uint32 startRam;

    char *tags;
    uint32 kernelSize;
    const char **kernelPages;
    uint32 initrdSize;
    const char **initrdPages;
};

// Copy memory (need a memcpy with __preload tag).
static void __preload
do_copy(char *dest, const char *src, int count)
{
    uint32 *d = (uint32*)dest, *s = (uint32*)src, *e = (uint32*)&src[count];
    while (s < e)
        *d++ = *s++;
}

// Copy a list of pages to a linear area of memory
static void __preload
do_copyPages(char *dest, const char **pages, int bytes)
{
    while (bytes > 0) {
        do_copy(dest, *pages, PAGE_SIZE);
        pages++;
        dest += PAGE_SIZE;
        bytes -= PAGE_SIZE;
    }
}

// Draw a line directly onto the frame buffer.
static void __preload
drawLine(uint32 *pvideoRam, uint16 color)
{
    enum { LINELENGTH = 2500 };
    uint32 videoRam = *pvideoRam;
    if (! videoRam)
        return;
    uint16 *pix = (uint16*)videoRam;
    for (int i = 0; i < LINELENGTH; i++)
        pix[32768+i] = color;
    *pvideoRam += LINELENGTH * sizeof(uint16);
}

// Code to launch kernel.
static void __preload
preloader(struct preloadData *data)
{
    drawLine(&data->videoRam, COLOR_BLUE);

    // Copy tags to beginning of ram.
    char *destTags = (char *)data->startRam + PHYSOFFSET_TAGS;
    do_copy(destTags, data->tags, PAGE_SIZE);

    drawLine(&data->videoRam, COLOR_RED);

    // Copy kernel image
    uint32 destKernel = data->startRam + PHYSOFFSET_KERNEL;
    do_copyPages((char *)destKernel, data->kernelPages, data->kernelSize);

    drawLine(&data->videoRam, COLOR_CYAN);

    // Copy initrd (if applicable)
    if (data->initrdSize)
        do_copyPages((char *)data->startRam + PHYSOFFSET_INITRD
                     , data->initrdPages, data->initrdSize);

    drawLine(&data->videoRam, COLOR_BLACK);

    // Boot
    typedef void (*lin_t)(uint32 zero, uint32 mach, char *tags);
    lin_t startfunc = (lin_t)destKernel;
    startfunc(0, data->machtype, destTags);
}


/****************************************************************
 * Physical ram kernel allocation and setup
 ****************************************************************/

extern "C" {
    // Asm code
    extern struct stackJumper_s stackJumper;

    // Symbols added by linker.
    extern char preload_start;
    extern char preload_end;
}
#define preload_size (&preload_end - &preload_start)
#define preloadExecOffset ((char *)&preloader - &preload_start)
#define stackJumperOffset ((char *)&stackJumper - &preload_start)
#define stackJumperExecOffset (stackJumperOffset        \
    + (uint32)&((stackJumper_s*)0)->asm_handler)

// Layout of an assembler function that can setup a C stack and entry
// point.  DO NOT CHANGE HERE without also upgrading the assembler
// code.
struct stackJumper_s {
    uint32 stack;
    uint32 data;
    uint32 execCode;
    char asm_handler[1];
};

static const int MaxImagePages = PAGE_SIZE / sizeof(uint32);

struct pagedata {
    uint32 physLoc;
    char *virtLoc;
};

static int physPageComp(const void *e1, const void *e2) {
    pagedata *i1 = (pagedata*)e1, *i2 = (pagedata*)e2;
    return (i1->physLoc < i2->physLoc ? -1
            : (i2->physLoc > i2->physLoc ? 1 : 0));
}

// Description of memory alocated by prepForKernel()
struct bootmem {
    char *kernelPages[MaxImagePages];
    char *initrdPages[MaxImagePages];
    uint32 physExec;
    void *allocedRam;
};

static void
cleanupBootMem(struct bootmem *bm)
{
    if (!bm)
        return;
    free(bm->allocedRam);
    free(bm);
}

// Allocate a continuous are of memory for a kernel (and possibly
// initrd), and configure a preloader that can launch that kernel.
// The resulting data is allocated in physically continuous ram that
// the caller can jump to when the MMU is disabled.  Note the caller
// needs to copy the kernel and initrd into this ram.
static bootmem *
prepForKernel(uint32 kernelSize, uint32 initrdSize)
{
    // Sanity test.
    if (preload_size > PAGE_SIZE || sizeof(preloadData) > PAGE_SIZE) {
        Output("Internal error.  Preloader too large");
        return NULL;
    }

    // Determine machine type
    uint32 machType = bootMachineType;
    if (! machType)
        machType = Mach->machType;
    if (! machType) {
        Complain(C_ERROR("undefined MTYPE"));
        return NULL;
    }
    Output("boot MTYPE=%d CMDLINE='%s'", machType, bootCmdline);

    // Allocate ram for kernel/initrd
    int kernelPages = PAGE_ALIGN(kernelSize) / PAGE_SIZE;
    int initrdPages = PAGE_ALIGN(initrdSize) / PAGE_SIZE;
    int totalPages = kernelPages + initrdPages + 6;
    if (kernelPages > MaxImagePages || initrdPages > MaxImagePages) {
        Output("Image too large - largest size is %d"
               , MaxImagePages * PAGE_SIZE);
        return NULL;
    }
    void *data = calloc(totalPages * PAGE_SIZE + PAGE_SIZE - 1, 1);
    if (! data) {
        Output("Failed to allocate %d pages", totalPages);
        return NULL;
    }

    // Allocate data structure.
    struct bootmem *bm = (bootmem*)calloc(sizeof(bootmem), 1);
    if (!bm) {
        Output("Failed to allocate bootmem struct");
        free(data);
        return NULL;
    }
    bm->allocedRam = data;

    // Find all the physical locations of the pages.
    data = (void*)PAGE_ALIGN((uint32)data);
    struct pagedata pages[MaxImagePages * 2 + 6];
    for (int i=0; i<totalPages; i++) {
        struct pagedata *pd = &pages[i];
        pd->virtLoc = &((char *)data)[PAGE_SIZE * i];
        pd->physLoc = memVirtToPhys((uint32)pd->virtLoc);
        if (pd->physLoc == (uint32)-1) {
            Output("Page at %p not mapped", pd->virtLoc);
            cleanupBootMem(bm);
            return NULL;
        }
    }

    // Sort the pages by physical location.
    qsort(pages, totalPages, sizeof(pages[0]), physPageComp);

    struct pagedata *pg_tag = &pages[0];
    struct pagedata *pgs_kernel = &pages[1];
    struct pagedata *pgs_initrd = &pages[kernelPages+1];
    struct pagedata *pg_kernelIndex = &pages[totalPages-5];
    struct pagedata *pg_initrdIndex = &pages[totalPages-4];
    struct pagedata *pg_stack = &pages[totalPages-3];
    struct pagedata *pg_data = &pages[totalPages-2];
    struct pagedata *pg_preload = &pages[totalPages-1];

    Output("Allocated %d pages (tags=%p/%08x kernel=%p/%08x initrd=%p/%08x)"
           , totalPages
           , pg_tag->virtLoc, pg_tag->physLoc
           , pgs_kernel->virtLoc, pgs_kernel->physLoc
           , pgs_initrd->virtLoc, pgs_initrd->physLoc);

    if (pg_tag->physLoc < memPhysAddr + PHYSOFFSET_TAGS
        || pgs_kernel->physLoc < memPhysAddr + PHYSOFFSET_KERNEL
        || (initrdSize
            && pgs_initrd->physLoc < memPhysAddr + PHYSOFFSET_INITRD)) {
        Output("Allocated memory will overwrite itself");
        cleanupBootMem(bm);
        return NULL;
    }

    // Setup linux tags.
    setup_linux_params(pg_tag->virtLoc, memPhysAddr + PHYSOFFSET_INITRD
                       , initrdSize);

    // Setup kernel/initrd indexes
    uint32 *index = (uint32*)pg_kernelIndex->virtLoc;
    for (int i=0; i<kernelPages; i++) {
        index[i] = pgs_kernel[i].physLoc;
        bm->kernelPages[i] = pgs_kernel[i].virtLoc;
    }
    index = (uint32*)pg_initrdIndex->virtLoc;
    for (int i=0; i<initrdPages; i++) {
        index[i] = pgs_initrd[i].physLoc;
        bm->initrdPages[i] = pgs_initrd[i].virtLoc;
    }

    // Setup preloader data.
    struct preloadData *pd = (struct preloadData *)pg_data->virtLoc;
    pd->machtype = machType;
    pd->tags = (char *)pg_tag->physLoc;
    pd->kernelSize = kernelSize;
    pd->kernelPages = (const char **)pg_kernelIndex->physLoc;
    pd->initrdSize = initrdSize;
    pd->initrdPages = (const char **)pg_initrdIndex->physLoc;
    pd->startRam = memPhysAddr;
    pd->videoRam = 0;

    if (Mach->fbDuringBoot) {
        pd->videoRam = vidGetVRAM();
        Output("Video buffer at phys=%08x", pd->videoRam);
    }

    // Setup preloader code.
    memcpy(pg_preload->virtLoc, &preload_start, preload_size);

    stackJumper_s *sj = (stackJumper_s*)&pg_preload->virtLoc[stackJumperOffset];
    sj->stack = pg_stack->physLoc + PAGE_SIZE;
    sj->data = pg_data->physLoc;
    sj->execCode = pg_preload->physLoc + preloadExecOffset;

    bm->physExec = pg_preload->physLoc + stackJumperExecOffset;

    Output("preload=%d@%p/%08x sj=%p stack=%p/%08x data=%p/%08x exec=%08x"
           , preload_size, pg_preload->virtLoc, pg_preload->physLoc
           , sj, pg_stack->virtLoc, pg_stack->physLoc
           , pg_data->virtLoc, pg_data->physLoc, sj->execCode);

    return bm;
}


/****************************************************************
 * Hardware shutdown and trampoline setup
 ****************************************************************/

extern "C" {
    // Assembler code
    void mmu_trampoline(uint32 phys, uint8 *mmu, uint32 code);
    void mmu_trampoline_end();
}

// Verify the mmu-disabling trampoline.
static uint32
setupTrampoline()
{
    uint32 virtTram = MVAddr((uint32)mmu_trampoline);
    uint32 virtTramEnd = MVAddr((uint32)mmu_trampoline_end);
    if ((virtTram & 0xFFFFF000) != (virtTramEnd & 0xFFFFF000)) {
        Output("Can't handle trampoline spanning page boundary"
               " (%p %08x %08x)"
               , mmu_trampoline, virtTram, virtTramEnd);
        return 0;
    }
    uint32 physAddrTram = memVirtToPhys(virtTram);
    if (physAddrTram == (uint32)-1) {
        Output("Trampoline not in physical ram. (virt=%08x)"
               , virtTram);
        return 0;
    }
    uint32 physTramL1 = physAddrTram & 0xFFF00000;
    if (virtTram > physTramL1 && virtTram < (physTramL1 + 0x100000)) {
        Output("Trampoline physical/virtual addresses overlap.");
        return 0;
    }

    Output("Trampoline setup (tram=%d@%p/%08x/%08x)"
           , virtTramEnd - virtTram, mmu_trampoline, virtTram, physAddrTram);

    return physAddrTram;
}

// Launch a kernel loaded in physical memory.
static void
launchKernel(uint32 physExec)
{
    // Make sure trampoline and "Mach->hardwareShutdown" functions are
    // loaded into memory.
    touchAppPages();

    // Prep the trampoline.
    uint32 physAddrTram = setupTrampoline();
    if (! physAddrTram)
        return;

    // Cache an mmu pointer for the trampoline
    uint8 *virtAddrMmu = memPhysMap(cpuGetMMU());
    Output("MMU setup: mmu=%p/%08x", virtAddrMmu, cpuGetMMU());

    // Lookup framebuffer address (if in use).
    uint32 vidRam = 0;
    if (Mach->fbDuringBoot) {
        vidRam = (uint32)vidGetVirtVRAM();
        Output("Video buffer at virt=%08x", vidRam);
    }

    // Call per-arch setup.
    int ret = Mach->preHardwareShutdown();
    if (ret)
        return;

    Screen("Go Go Go...");

    // Disable interrupts
    take_control();

    drawLine(&vidRam, COLOR_GREEN);

    // Call per-arch boot prep function.
    Mach->hardwareShutdown();

    drawLine(&vidRam, COLOR_MAGENTA);

    // Disable MMU and launch linux.
    mmu_trampoline(physAddrTram, virtAddrMmu, physExec);

    // The above should not ever return, but we attempt recovery here.
    return_control();
}


/****************************************************************
 * File reading
 ****************************************************************/

// Open a file on disk.
static FILE *
file_open(const char *name)
{
    Output("Opening file %s", name);
    char fn[200];
    fnprepare(name, fn, sizeof(fn));
    FILE *fk = fopen(fn, "rb");
    if (!fk) {
        Output("Failed to load file %s", fn);
        return NULL;
    }
    return fk;
}

// Find out the size of an open file.
static uint32
get_file_size(FILE *fk)
{
    fseek(fk, 0, SEEK_END);
    uint32 size = ftell(fk);
    fseek(fk, 0, SEEK_SET);
    return size;
}

// Copy data from a file into memory and check for success.
static int
file_read(FILE *f, char **pages, uint32 size)
{
    Output("Reading %d bytes...", size);
    while (size) {
        uint32 s = size < PAGE_SIZE ? size : PAGE_SIZE;
        uint32 ret = fread(*pages, 1, s, f);
        if (ret != s) {
            Output("Error reading file.  Expected %d got %d", s, ret);
            return -1;
        }
        pages++;
        size -= s;
        AddProgress(s);
    }
    Output("Read complete");
    return 0;
}

// Load a kernel (and possibly initrd) from disk into physically
// continous ram and prep it for kernel starting.
static bootmem *
loadDiskKernel()
{
    Output("boot KERNEL=%s INITRD=%s", bootKernel, bootInitrd);

    // Open kernel file
    FILE *kernelFile = file_open(bootKernel);
    if (!kernelFile)
        return NULL;
    uint32 kernelSize = get_file_size(kernelFile);

    // Open initrd file
    FILE *initrdFile = NULL;
    uint32 initrdSize = 0;
    if (bootInitrd && *bootInitrd) {
        initrdFile = file_open(bootInitrd);
        if (initrdFile)
            initrdSize = get_file_size(initrdFile);
    }

    // Obtain physically continous ram for the kernel
    int ret;
    struct bootmem *bm = NULL;
    bm = prepForKernel(kernelSize, initrdSize);
    if (!bm)
        goto abort;

    InitProgress(kernelSize + initrdSize);

    // Load kernel
    ret = file_read(kernelFile, bm->kernelPages, kernelSize);
    if (ret)
        goto abort;
    // Load initrd
    if (initrdFile) {
        ret = file_read(initrdFile, bm->initrdPages, initrdSize);
        if (ret)
            goto abort;
    }

    fclose(kernelFile);
    if (initrdFile)
        fclose(initrdFile);

    DoneProgress();

    return bm;

abort:
    DoneProgress();

    if (initrdFile)
        fclose(initrdFile);
    if (kernelFile)
        fclose(kernelFile);
    cleanupBootMem(bm);
    return NULL;
}


/****************************************************************
 * Resume vector hooking
 ****************************************************************/

static uint32 winceResumeAddr = 0xa0040000;

// Setup a kernel in physical ram and hook the wince resume vector so
// that it runs on resume.
static void
resumeIntoBoot(uint32 physExec)
{
    // Lookup wince resume address and verify it looks sane.
    uint32 *resume = (uint32*)memPhysMap(winceResumeAddr);
    if (!resume) {
        Output("Could not map addr %08x", winceResumeAddr);
        return;
    }
    // Check for "b 0x41000 ; 0x0" at the address.
    uint32 old1 = resume[0], old2 = resume[1];
    if (old1 != 0xea0003fe || old2 != 0x0) {
        Output("Unexpected resume vector. (%08x %08x)", old1, old2);
        return;
    }

    // Overwrite the resume vector.
    take_control();
    cpuFlushCache();
    resume[0] = 0xe51ff004; // ldr pc, [pc, #-4]
    resume[1] = physExec;
    return_control();

    // Wait for user to suspend/resume
    Screen("Ready to boot.  Please suspend/resume");
    Sleep(300 * 1000);

    // Cleanup (if boot failed somehow).
    Output("Timeout. Restoring original resume vector");
    take_control();
    cpuFlushCache();
    resume[0] = old1;
    resume[1] = old2;
    return_control();
}


/****************************************************************
 * Boot code
 ****************************************************************/

// Simple switch on booting mechanisms.
static void
tryLaunch(uint32 physExec, int bootViaResume)
{
    Output("Launching to physical address %08x", physExec);
    if (bootViaResume)
        resumeIntoBoot(physExec);
    else
        launchKernel(physExec);
}

// Load a kernel from disk, disable hardware, and jump into kernel.
static void
bootLinux(const char *cmd, const char *args)
{
    int bootViaResume = toupper(cmd[0]) == 'R';

    // Load the kernel/initrd/tags/preloader into physical memory
    struct bootmem *bm = loadDiskKernel();
    if (!bm)
        return;

    // Luanch it.
    tryLaunch(bm->physExec, bootViaResume);

    // Cleanup (if boot failed somehow).
    cleanupBootMem(bm);
}
REG_CMD(0, "BOOT|LINUX", bootLinux,
        "BOOTLINUX\n"
        "  Start booting linux kernel. See HELP VARS for variables affecting boot.")
REG_CMD_ALT(0, "BOOT2", bootLinux, boot2, 0)
REG_CMD_ALT(
    0, "RESUMEINTOBOOT", bootLinux, resumeintoboot,
    "RESUMEINTOBOOT\n"
    "  Overwrite the wince resume vector so that the kernel boots\n"
    "  after suspending/resuming the pda")


/****************************************************************
 * Boot from kernel already in ram
 ****************************************************************/

static void
copy_pages(char **pages, const char *src, uint32 size)
{
    while (size) {
        uint32 s = size < PAGE_SIZE ? size : PAGE_SIZE;
        memcpy(*pages, src, s);
        src += s;
        pages++;
        size -= s;
        AddProgress(s);
    }
}

// Load a kernel already in memory, disable hardware, and jump into
// kernel.
void
bootRamLinux(const char *kernel, uint32 kernelSize
             , const char *initrd, uint32 initrdSize
             , int bootViaResume)
{
    // Obtain physically continous ram for the kernel
    struct bootmem *bm = prepForKernel(kernelSize, initrdSize);
    if (!bm)
        return;

    // Copy kernel / initrd.
    InitProgress(kernelSize + initrdSize);
    copy_pages(bm->kernelPages, kernel, kernelSize);
    copy_pages(bm->initrdPages, initrd, initrdSize);
    DoneProgress();

    // Luanch it.
    tryLaunch(bm->physExec, bootViaResume);

    // Cleanup (if boot failed somehow).
    cleanupBootMem(bm);
}
