#include "main.h"

PSP_MODULE_INFO("me-csc-hd", 0, 1, 1);
PSP_HEAP_SIZE_KB(-1024);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_VFPU | PSP_THREAD_ATTR_USER);

static u32 DST_BUFFER_RGB;

#define STRIDE                1024
#define FRAME_YCBCR_WIDTH     992
#define FRAME_YCBCR_HEIGHT    992
constexpr u32 BUFFER_2_OFFSET   = STRIDE * FRAME_YCBCR_HEIGHT * 2;
constexpr u32 FRAME_YCBCR_SIZE  = FRAME_YCBCR_WIDTH * FRAME_YCBCR_HEIGHT;
constexpr u32 Y_SIZE            = FRAME_YCBCR_SIZE;
constexpr u32 CBCR_SIZE         = FRAME_YCBCR_SIZE / 4;
constexpr u32 BLOCKS_WCOUNT     = FRAME_YCBCR_WIDTH / 16;
constexpr u32 BLOCKS_HCOUNT     = FRAME_YCBCR_HEIGHT / 16;

// Set up the me shared variables in uncached user memory.
static volatile u32* mem = nullptr;
#define y         (mem[0])
#define cb        (mem[1])
#define cr        (mem[2])
#define meExit    (mem[3])

extern char __start__me_section;
extern char __stop__me_section;
__attribute__((section("_me_section")))
void meHandler() {
  hw(0xbc100050) = 0x7f;
  hw(0xbc100004) = 0xffffffff;
  hw(0xbc100040) = 0x02;
  asm("sync");
  // Wait until mem is ready
  while (!mem || !y) {
    meDcacheWritebackInvalidateAll();
  }
  meExit = 1;
  meHalt();
}

static int initMe() {
  #define me_section_size (&__stop__me_section - &__start__me_section)
  memcpy((void *)ME_HANDLER_BASE, (void*)&__start__me_section, me_section_size);
  sceKernelDcacheWritebackInvalidateAll();
  hw(0xbc10004c) = 0x04;
  hw(0xbc10004c) = 0x0;
  asm volatile("sync");
  return 0;
}

#define MOVE_STEP 2
static unsigned int __attribute__((aligned(16))) list[1024] = {0};

// Update frame buffer destination
static void updateDisplayBuffer(SceCtrlData& ctl) {
  static u32 l = 0;
  static u32 t = 0;
  if ((ctl.Buttons & PSP_CTRL_LEFT) && l >= MOVE_STEP) {
    l -= MOVE_STEP;
  }
  if ((ctl.Buttons & PSP_CTRL_RIGHT) && l <= (FRAME_YCBCR_WIDTH - 480 - MOVE_STEP)) {
    l += MOVE_STEP;
  }
  if ((ctl.Buttons & PSP_CTRL_UP) && t >= MOVE_STEP) {
    t -= MOVE_STEP;
  }
  if ((ctl.Buttons & PSP_CTRL_DOWN) && t <= (FRAME_YCBCR_HEIGHT - 272 - MOVE_STEP)) {
    t += MOVE_STEP;
  }
  sceGuStart(GU_DIRECT, list);
  sceGuCopyImage(GU_PSM_8888, l, t, 480, 272, STRIDE, (void*)DST_BUFFER_RGB,
    0, 0, 512, (void*)(UNCACHED_USER_MASK | GE_EDRAM_BASE));
  sceGuFinish();
  sceGuSync(0,0);
}

static int startCSC() {
  // start csc rendering
  hw(0xBC800160) = 1;
  asm("sync");
  return 0;
}

// Set up color space conversion hardware registers
static int setupCSC() {  
  // Set buffer 1 sources
  hw(0xBC800120) = y;
  hw(0xBC800130) = cb;
  hw(0xBC800134) = cr;
  
  // Set buffer 2 sources
  hw(0xBC800128) = y + Y_SIZE / 2;
  hw(0xBC800138) = cb + CBCR_SIZE / 2;
  hw(0xBC80013C) = cr + CBCR_SIZE / 2;
  asm volatile("sync");

  // bit [22:16] height/16 | bit [13:8] width/16 | bit[2:1] ignore dest 2 | line replication control | use avc/vme
  hw(0xBC800140) = (BLOCKS_HCOUNT << 16) | (BLOCKS_WCOUNT << 8) | 0 << 2 | 1 << 1 | 1;
  hw(0xBC800144) = DST_BUFFER_RGB;
  hw(0xBC800148) = DST_BUFFER_RGB + BUFFER_2_OFFSET;
  
  // bit [...8] stride | bit[1] pixel format (0: 8888, 1: 5650) | bit[0] separate dst 2 rendering
  hw(0xBC80014C) = (STRIDE << 8) | 0 << 1 | 1;
  
  // Use default matrice values, with adjusted luma
  const float brightness = 1.16f;
  hw(0xBC800150) = 0x0CC << 20 | 0x000 << 10 | q37(brightness); // r
  hw(0xBC800154) = 0x398 << 20 | 0x3CE << 10 | q37(brightness); // g
  hw(0xBC800158) = 0x000 << 20 | 0x102 << 10 | q37(brightness); // b
  asm("sync");
  
  return 0;
}

int main() {
  scePowerSetClockFrequency(333, 333, 166);
  if (pspSdkLoadStartModule("ms0:/PSP/GAME/me/kcall.prx", PSP_MEMORY_PARTITION_KERNEL) < 0){
    sceKernelExitGame();
    return 0;
  }

  sceGuInit();
  pspDebugScreenInitEx(0x0, PSP_DISPLAY_PIXEL_FORMAT_8888, 0);
  sceDisplaySetFrameBuf((void*)(UNCACHED_USER_MASK | GE_EDRAM_BASE),
    512, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
  
  pspDebugScreenPrintf("Loading YCbCr planes...\n");
  meGetUncached32(&mem, 4);

  u32* const _buff = (u32*)memalign(16, 1024*1024*4);
  DST_BUFFER_RGB = UNCACHED_USER_MASK | (u32)_buff;
  
  // Load Y, Cb and Cr
  u8* const _y = getByteFromFile("y_hd.bin", Y_SIZE);
  u8* const _cb = getByteFromFile("cb_hd.bin", CBCR_SIZE);
  u8* const _cr = getByteFromFile("cr_hd.bin", CBCR_SIZE);
  if (!_y || !_cb || !_cr) {
    sceKernelExitGame();
  }
  
  // Ensure that Y, Cb, and Cr are available in the cache before performing uncached accesses
  sceKernelDcacheWritebackInvalidateAll();
  y = UNCACHED_USER_MASK | (u32)_y;
  cb = UNCACHED_USER_MASK | (u32)_cb;
  cr = UNCACHED_USER_MASK | (u32)_cr;

  // Setup the minimal Me config
  kcall(&initMe);
  do {
    asm volatile("sync");
  } while(!meExit);

  // Setup csc hardware registers
  kcall(&setupCSC);
  kcall(&startCSC);
  
  pspDebugScreenClear();
  SceCtrlData ctl;
  do {
    sceCtrlPeekBufferPositive(&ctl, 1);
    updateDisplayBuffer(ctl);
    sceDisplayWaitVblankStart();
  } while(!(ctl.Buttons & PSP_CTRL_HOME));
  
  // clean y,cb,cr planes
  free(_y);
  free(_cb);
  free(_cr);
  free(_buff);
  
  // clean allocated me user memory
  meGetUncached32(&mem, 0);
  
  // exit
  pspDebugScreenClear();
  pspDebugScreenPrintf("Exiting...");
  sceKernelDelayThread(500000);
  sceKernelExitGame();
  return 0;
}
