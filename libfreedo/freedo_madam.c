/*
  www.freedo.org
  The first and only working 3DO multiplayer emulator.

  The FreeDO licensed under modified GNU LGPL, with following notes:

  *   The owners and original authors of the FreeDO have full right to
  *   develop closed source derivative work.

  *   Any non-commercial uses of the FreeDO sources or any knowledge
  *   obtained by studying or reverse engineering of the sources, or
  *   any other material published by FreeDO have to be accompanied
  *   with full credits.

  *   Any commercial uses of FreeDO sources or any knowledge obtained
  *   by studying or reverse engineering of the sources, or any other
  *   material published by FreeDO is strictly forbidden without
  *   owners approval.

  The above notes are taking precedence over GNU LGPL in conflicting
  situations.

  Project authors:
  *  Alexander Troosh
  *  Maxim Grishin
  *  Allen Wright
  *  John Sammons
  *  Felix Lazarev
*/

#include "freedo_arm.h"
#include "freedo_bitop.h"
#include "freedo_clio.h"
#include "freedo_core.h"
#include "freedo_madam.h"
#include "freedo_pbus.h"
#include "freedo_vdlp.h"
#include "hack_flags.h"
#include "inline.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

struct BitReaderBig bitoper;

struct SCB
{
  uint32_t flags;
  uint32_t next;
  uint32_t data;
  uint32_t PIPptr;
  int32_t  X;
  int32_t  Y;
  int32_t  HDX;
  int32_t  HDY;
  int32_t  VDX;
  int32_t  VDY;
  int32_t  DDX;
  int32_t  DDY;
  uint32_t PPMPC;
  uint32_t PRE0;
  uint32_t PRE1;
};


typedef struct SCB SCB;

/* === SCB control word flags === */
#define SCB_SKIP        0x80000000
#define SCB_LAST        0x40000000
#define SCB_NPABS       0x20000000
#define SCB_SPABS       0x10000000
#define SCB_PPABS       0x08000000
#define SCB_LDSIZE      0x04000000
#define SCB_LDPRS       0x02000000
#define SCB_LDPPMP      0x01000000
#define SCB_LDPLUT      0x00800000
#define SCB_SCBPRE      0x00400000
#define SCB_YOXY        0x00200000
#define SCB_ACSC        0x00100000
#define SCB_ALSC        0x00080000
#define SCB_ACW         0x00040000
#define SCB_ACCW        0x00020000
#define SCB_TWD         0x00010000
#define SCB_LCE         0x00008000
#define SCB_ACE         0x00004000
#define SCB_reserved13  0x00002000
#define SCB_MARIA       0x00001000
#define SCB_PXOR        0x00000800
#define SCB_USEAV       0x00000400
#define SCB_PACKED      0x00000200
#define SCB_POVER_MASK  0x00000180
#define SCB_PLUTPOS     0x00000040
#define SCB_BGND        0x00000020
#define SCB_NOBLK       0x00000010
#define SCB_PLUTA_MASK  0x0000000F

#define SCB_POVER_SHIFT  7
#define SCB_PLUTA_SHIFT  0

#define PMODE_PDC   ((0x00000000)<<SCB_POVER_SHIFT) /* Normal */
#define PMODE_ZERO  ((0x00000002)<<SCB_POVER_SHIFT)
#define PMODE_ONE   ((0x00000003)<<SCB_POVER_SHIFT)

/* === SCBCTL0 flags === */
#define B15POS_MASK   0xC0000000
#define B0POS_MASK    0x30000000
#define SWAPHV        0x08000000
#define ASCALL        0x04000000
#define _SCBCTL0_u25  0x02000000
#define CFBDSUB       0x01000000
#define CFBDLSB_MASK  0x00C00000
#define PDCLSB_MASK   0x00300000

#define B15POS_SHIFT  30
#define B0POS_SHIFT   28
#define CFBD_SHIFT    22
#define PDCLSB_SHIFT  20

/* B15POS_MASK definitions */
#define B15POS_0    0x00000000
#define B15POS_1    0x40000000
#define B15POS_PDC  0xC0000000

/* B0POS_MASK definitions */
#define B0POS_0     0x00000000
#define B0POS_1     0x10000000
#define B0POS_PPMP  0x20000000
#define B0POS_PDC   0x30000000

/* CFBDLSB_MASK definitions */
#define CFBDLSB_0      0x00000000
#define CFBDLSB_CFBD0  0x00400000
#define CFBDLSB_CFBD4  0x00800000
#define CFBDLSB_CFBD5  0x00C00000

/* PDCLSB_MASK definitions */
#define PDCLSB_0     0x00000000
#define PDCLSB_PDC0  0x00100000
#define PDCLSB_PDC4  0x00200000
#define PDCLSB_PDC5  0x00300000

/* === Cel first preamble word flags === */
#define PRE0_LITERAL    0x80000000
#define PRE0_BGND       0x40000000
#define PREO_reservedA  0x30000000
#define PRE0_SKIPX_MASK 0x0F000000
#define PREO_reservedB  0x00FF0000
#define PRE0_VCNT_MASK  0x0000FFC0
#define PREO_reservedC  0x00000020
#define PRE0_LINEAR     0x00000010
#define PRE0_REP8       0x00000008
#define PRE0_BPP_MASK   0x00000007

#define PRE0_SKIPX_SHIFT 24
#define PRE0_VCNT_SHIFT  6
#define PRE0_BPP_SHIFT   0

/* PRE0_BPP_MASK definitions */
#define PRE0_BPP_1   0x00000001
#define PRE0_BPP_2   0x00000002
#define PRE0_BPP_4   0x00000003
#define PRE0_BPP_6   0x00000004
#define PRE0_BPP_8   0x00000005
#define PRE0_BPP_16  0x00000006

/* Subtract this value from the actual vertical source line count */
#define PRE0_VCNT_PREFETCH    1

/* === Cel second preamble word flags === */
#define PRE1_WOFFSET8_MASK   0xFF000000
#define PRE1_WOFFSET10_MASK  0x03FF0000
#define PRE1_NOSWAP          0x00004000
#define PRE1_TLLSB_MASK      0x00003000
#define PRE1_LRFORM          0x00000800
#define PRE1_TLHPCNT_MASK    0x000007FF

#define PRE1_WOFFSET8_SHIFT   24
#define PRE1_WOFFSET10_SHIFT  16
#define PRE1_TLLSB_SHIFT      12
#define PRE1_TLHPCNT_SHIFT    0

#define PRE1_TLLSB_0     0x00000000
#define PRE1_TLLSB_PDC0  0x00001000 /* Normal */
#define PRE1_TLLSB_PDC4  0x00002000
#define PRE1_TLLSB_PDC5  0x00003000

/* Subtract this value from the actual word offset */
#define PRE1_WOFFSET_PREFETCH 2
/* Subtract this value from the actual pixel count */
#define PRE1_TLHPCNT_PREFETCH 1

#define PPMP_0_SHIFT 0
#define PPMP_1_SHIFT 16

#define PPMPC_1S_MASK  0x00008000
#define PPMPC_MS_MASK  0x00006000
#define PPMPC_MF_MASK  0x00001C00
#define PPMPC_SF_MASK  0x00000300
#define PPMPC_2S_MASK  0x000000C0
#define PPMPC_AV_MASK  0x0000003E
#define PPMPC_2D_MASK  0x00000001

#define PPMPC_MS_SHIFT  13
#define PPMPC_MF_SHIFT  10
#define PPMPC_SF_SHIFT  8
#define PPMPC_2S_SHIFT  6
#define PPMPC_AV_SHIFT  1

/* PPMPC_1S_MASK definitions */
#define PPMPC_1S_PDC   0x00000000
#define PPMPC_1S_CFBD  0x00008000

/* PPMPC_MS_MASK definitions */
#define PPMPC_MS_SCB         0x00000000
#define PPMPC_MS_PIN         0x00002000
#define PPMPC_MS_PDC_MFONLY  0x00004000
#define PPMPC_MS_PDC         0x00004000

/* PPMPC_MF_MASK definitions */
#define PPMPC_MF_1  0x00000000
#define PPMPC_MF_2  0x00000400
#define PPMPC_MF_3  0x00000800
#define PPMPC_MF_4  0x00000C00
#define PPMPC_MF_5  0x00001000
#define PPMPC_MF_6  0x00001400
#define PPMPC_MF_7  0x00001800
#define PPMPC_MF_8  0x00001C00

/* PPMPC_SF_MASK definitions */
#define PPMPC_SF_2   0x00000100
#define PPMPC_SF_4   0x00000200
#define PPMPC_SF_8   0x00000300
#define PPMPC_SF_16  0x00000000

/* PPMPC_2S_MASK definitions */
#define PPMPC_2S_0     0x00000000
#define PPMPC_2S_SCB   0x00000040
#define PPMPC_2S_CFBD  0x00000080
#define PPMPC_2S_PDC   0x000000C0

/* PPMPC_2D_MASK definitions */
#define PPMPC_2D_1  0x00000000
#define PPMPC_2D_2  0x00000001


#pragma pack(push,1)

struct cp1btag_s
{
  uint16_t c:1;
  uint16_t pad:15;
};

typedef struct cp1btag_s cp1btag_t;

struct cp2btag_s
{
  uint16_t c:2;
  uint16_t pad:14;
};

typedef struct cp2btag_s cp2btag_t;

struct cp4btag_s
{
  uint16_t c:4;
  uint16_t pad:12;
};

typedef struct cp4btag_s cp4btag_t;

struct cp6btag_s
{
  uint16_t c:5;
  uint16_t pw:1;
  uint16_t pad:10;
};

typedef struct cp6btag_s cp6btag_t;

struct cp8btag_s
{
  uint16_t c:5;
  uint16_t mpw:1;
  uint16_t m:2;
  uint16_t pad:8;
};

typedef struct cp8btag_s cp8btag_t;

struct cp16btag_s
{
  uint16_t c:5;
  uint16_t mb:3;
  uint16_t mg:3;
  uint16_t mr:3;
  uint16_t pad:1;
  uint16_t pw:1;
};

typedef struct cp16btag_s cp16btag_t;

struct up8btag_s
{
  uint16_t b:2;
  uint16_t g:3;
  uint16_t r:3;
  uint16_t pad:8;
};

typedef struct up8btag_s up8btag_t;

struct up16btag_s
{
  uint16_t bw:1;
  uint16_t b:4;
  uint16_t g:5;
  uint16_t r:5;
  uint16_t p:1;
};

typedef struct up16btag_s up16btag_t;

struct res16btag_s
{
  uint16_t b:5;
  uint16_t g:5;
  uint16_t r:5;
  uint16_t p:1;
};

typedef struct res16btag_s res16btag_t;

union pdeco_u
{
  uint32_t    raw;
  cp1btag_t   c1b;
  cp2btag_t   c2b;
  cp4btag_t   c4b;
  cp6btag_t   c6b;
  cp8btag_t   c8b;
  cp16btag_t  c16b;
  up8btag_t   u8b;
  up16btag_t  u16b;
  res16btag_t r16b;
};

typedef union pdeco_u pdeco_t;

struct avtag_s
{
  uint8_t NEG:1;
  uint8_t XTEND:1;
  uint8_t nCLIP:1;
  uint8_t dv3:2;
  uint8_t pad:3;
};

typedef struct avtag_s avtag_t;

union AVS_u
{
  avtag_t  avsignal;
  uint32_t raw;
};

typedef union AVS_u AVS_t;

struct pixctag_s
{
  uint8_t dv2:1;
  uint8_t av:5;
  uint8_t s2:2;
  uint8_t dv1:2;
  uint8_t mxf:3;
  uint8_t ms:2;
  uint8_t s1:1;
};

typedef struct pixctag_s pixctag_t;

union PXC_u
{
  pixctag_t meaning;
  uint32_t  raw;
};

typedef union PXC_u PXC_t;

#define MADAM_REGISTER_COUNT   2048
#define MADAM_PLUT_COUNT       32

#pragma pack(pop)

struct madam_s
{
  uint32_t mregs[MADAM_REGISTER_COUNT+64];
  uint16_t PLUT[MADAM_PLUT_COUNT];
  int32_t  RMOD;
  int32_t  WMOD;
  uint32_t FSM;
};

typedef struct madam_s madam_t;

static madam_t MADAM;
static int     KPRINT = 0;

void
freedo_madam_kprint_set(const int v_)
{
  KPRINT = v_;
}

uint32_t
freedo_madam_fsm_get(void)
{
  return MADAM.FSM;
}

void
freedo_madam_fsm_set(uint32_t val_)
{
  MADAM.FSM = val_;
}

uint32_t
freedo_madam_state_size(void)
{
  return sizeof(madam_t);
}

void
freedo_madam_state_save(void *buf_)
{
  memcpy(buf_,&MADAM,sizeof(madam_t));
}

void
freedo_madam_state_load(const void *buf_)
{
  memcpy(&MADAM,buf_,sizeof(madam_t));
}

static uint32_t PXOR1;
static uint32_t PXOR2;

#define PBMASK 0x80000000
#define KUP    0x08000000
#define KDN    0x10000000
#define KRI    0x04000000
#define KLE    0x02000000
#define KA     0x01000000
#define KB     0x00800000
#define KC     0x00400000
#define KP     0x00200000
#define KX     0x00100000
#define KRS    0x00080000
#define KLS    0x00040000
#define FIXP16_SHIFT     16
#define FIXP16_MAG       65536
#define FIXP16_DP_MASK   0x0000ffff
#define FIXP16_WP_MASK   0xffff0000
#define FIXP16_ROUND_UP  0x0000ffff //0x8000

static uint32_t mread(uint32_t addr);
static void     mwrite(uint32_t addr, uint32_t val);
static int32_t  TestInitVisual(int32_t packed);
static int32_t  Init_Line_Map(void);
static void     Init_Scale_Map(void);
static void     Init_Arbitrary_Map(void);
static void     TexelDraw_Line(uint16_t CURPIX, uint16_t LAMV, int32_t xcur, int32_t ycur, int32_t cnt);
static int32_t  TexelDraw_Scale(uint16_t CURPIX, uint16_t LAMV, int32_t xcur, int32_t ycur, int32_t deltax, int32_t deltay);
static int32_t  TexelDraw_Arbitrary(uint16_t CURPIX, uint16_t LAMV, int32_t xA, int32_t yA, int32_t xB, int32_t yB, int32_t xC, int32_t yC, int32_t xD, int32_t yD);
static void     DrawPackedCel_New(void);
static void     DrawLiteralCel_New(void);
static void     DrawLRCel_New(void);
static void     HandleDMA8(void);
static void     DMAPBus(void);

/* general 3D vertex class */

#define INT1220(a)   ((int32_t)(a)>>20)
#define INT1220up(a) ((int32_t)((a)+(1<<19))>>20)

static struct
{
  uint32_t plutaSCBbits;
  uint32_t pixelBitsMask;
  bool     tmask;
} pdec;

static struct
{
  uint32_t pmode;
  uint32_t pmodeORmask;
  uint32_t pmodeANDmask;
  bool     Transparent;
} pproj;

static uint8_t  *Mem;
static uint32_t  retuval;
static uint32_t  BITADDR;
static uint32_t  BITBUFLEN;
static uint32_t  BITBUF;
static uint32_t  SCBFLAGS;
static uint32_t  PIXC;
static uint32_t  PRE0;
static uint32_t  PRE1;
static uint32_t  TARGETPROJ;
static uint32_t  SRCDATA;
static int32_t   SPRWI;
static int32_t   SPRHI;
static uint32_t  PLUTF;
static uint32_t  PDATF;
static uint32_t  NSCBF;
static int32_t   CELCYCLES;
static bool      ADD;
static int32_t   BITCALC;

static uint16_t bitbuf;
static uint8_t  subbitbuf;
static int32_t  bitcount;
static int64_t  compsize;  /* size of commpressed in bytes. actually pixcount*bpp/8 */
static uint32_t gFINISH;
static uint16_t RRR;
static int32_t  USECEL;

static uint32_t const BPP[8] = {1,1,2,4,6,8,16,1};

static uint8_t PSCALAR[8][4][32];

static uint16_t MAPu8b[256+64];
static uint16_t MAPc8bAMV[256+64];
static uint16_t MAPc16bAMV[8*8*8+64];

static int32_t  currentrow;
static uint32_t bpp;
static int32_t  pixcount;
static uint32_t type;
static uint32_t offsetl;
static uint32_t offset;
static uint32_t eor;
static int32_t  calcx;
static int32_t  nrows;

static uint32_t pix;
static uint16_t ttt;
static uint32_t OFF;
static uint32_t pSource;

//CelEngine STATBits
#define STATBITS	MADAM.mregs[0x28]

#define SPRON		0x10
#define SPRPAU		0x20

//CelEngine Registers
#define SPRSTRT		0x100
#define SPRSTOP		0x104
#define SPRCNTU		0x108
#define SPRPAUS		0x10c

#define SCBCTL0		MADAM.mregs[0x110]
#define REGCTL0		MADAM.mregs[0x130]
#define REGCTL1		MADAM.mregs[0x134]
#define REGCTL2		MADAM.mregs[0x138]
#define REGCTL3		MADAM.mregs[0x13c]

#define CLIPXVAL	((int)MADAM.mregs[0x134]&0x3ff)
#define CLIPYVAL	((int)(MADAM.mregs[0x134]>>16)&0x3ff)

#define PIXSOURCE	(MADAM.mregs[0x138])
#define FBTARGET	(MADAM.mregs[0x13c])

#define CURRENTSCB	MADAM.mregs[0x5a0]
//next ccb == 0 stop the engine
#define NEXTSCB		MADAM.mregs[0x5a4]
#define PLUTDATA	MADAM.mregs[0x5a8]
#define PDATA		MADAM.mregs[0x5ac]
#define ENGAFETCH	MADAM.mregs[0x5b0]
#define ENGALEN		MADAM.mregs[0x5b4]
#define	ENGBFETCH	MADAM.mregs[0x5b8]
#define ENGBLEN		MADAM.mregs[0x5bc]
#define PAL_EXP		(&MADAM.mregs[0x5d0])

static
INLINE
const
int32_t
PDV(const int32_t x_)
{
  return (((x_ - 1) & 3) + 1);
}

static
INLINE
const
int32_t
XY2OFF(const int32_t x_,
       const int32_t y_,
       const int32_t m_)
{
  return (((y_ >> 1) * m_) + ((y_ & 1) << 1) + (x_ << 2));
}

static
INLINE
const
uint32_t
TESTCLIP(const int32_t x_,
         const int32_t y_)
{
  return ((x_ >= 0) &&
          (x_ <= CLIPXVAL) &&
          (y_ >= 0) &&
          (y_ <= CLIPYVAL));
}

uint32_t
freedo_madam_peek(uint32_t addr_)
{
  /* we need to return actual fifo status */
  if((addr_ >= 0x400) && (addr_ <= 0x53F))
    return freedo_clio_fifo_read(addr_);

  /* status of CEL */
  if(addr_ == 0x28)
    {
      switch(MADAM.FSM)
        {
        case FSM_IDLE:
          return 0x00;
        case FSM_SUSPENDED:
          return 0x30;
        case FSM_INPROCESS:
          return 0x10;
        }
    }

  return MADAM.mregs[addr_];
}

/* Matrix engine macros */
#define M00  ((float)(int32_t)MADAM.mregs[0x600])
#define M01  ((float)(int32_t)MADAM.mregs[0x604])
#define M02  ((float)(int32_t)MADAM.mregs[0x608])
#define M03  ((float)(int32_t)MADAM.mregs[0x60C])
#define M10  ((float)(int32_t)MADAM.mregs[0x610])
#define M11  ((float)(int32_t)MADAM.mregs[0x614])
#define M12  ((float)(int32_t)MADAM.mregs[0x618])
#define M13  ((float)(int32_t)MADAM.mregs[0x61C])
#define M20  ((float)(int32_t)MADAM.mregs[0x620])
#define M21  ((float)(int32_t)MADAM.mregs[0x624])
#define M22  ((float)(int32_t)MADAM.mregs[0x628])
#define M23  ((float)(int32_t)MADAM.mregs[0x62C])
#define M30  ((float)(int32_t)MADAM.mregs[0x630])
#define M31  ((float)(int32_t)MADAM.mregs[0x634])
#define M32  ((float)(int32_t)MADAM.mregs[0x638])
#define M33  ((float)(int32_t)MADAM.mregs[0x63C])

#define  V0  ((float)(int32_t)MADAM.mregs[0x640])
#define  V1  ((float)(int32_t)MADAM.mregs[0x644])
#define  V2  ((float)(int32_t)MADAM.mregs[0x648])
#define  V3  ((float)(int32_t)MADAM.mregs[0x64C])

#define Rez0 MADAM.mregs[0x660]
#define Rez1 MADAM.mregs[0x664]
#define Rez2 MADAM.mregs[0x668]
#define Rez3 MADAM.mregs[0x66C]

#define Nfrac16 (((int64_t)MADAM.mregs[0x680]<<32)|(uint32_t)MADAM.mregs[0x684])

void
freedo_madam_poke(uint32_t addr_,
                  uint32_t val_)
{
  if((addr_ >= 0x400) && (addr_ <= 0x53F))
    return freedo_clio_fifo_write(addr_,val_);

  switch(addr_)
    {
    case 0x00:
      if(KPRINT)
        fputc(val_,stderr);
      return;
    case 0x04:
      val_               = 0x29;
      MADAM.mregs[addr_] = val_;
      break;
    case 0x08:
      MADAM.mregs[addr_] = val_;
      HandleDMA8();
      break;
    case 0x580:
      freedo_vdlp_process(val_);
      return;
    case 0x584:
    case 0x588:
    case 0x58C:
    case 0x590:
    case 0x594:
    case 0x598:
    case 0x59C:
      MADAM.mregs[addr_] = val_;
      return;
    case SPRSTRT:
      if(MADAM.FSM == FSM_IDLE)
        MADAM.FSM = FSM_INPROCESS;
      return;
    case SPRSTOP:
      MADAM.FSM = FSM_IDLE;
      NEXTSCB = 0;
      return;
    case SPRCNTU:
      if(MADAM.FSM == FSM_SUSPENDED)
        MADAM.FSM = FSM_INPROCESS;
      return;
    case SPRPAUS:
      if(MADAM.FSM == FSM_INPROCESS)
        MADAM.FSM = FSM_SUSPENDED;
      return;

      /* Matix engine */
    case 0x7FC:
      {
        static double Rez0T,Rez1T,Rez2T,Rez3T;

        MADAM.mregs[0x7fc] = 0;       /* matrix engine already ready */

        switch(val_)
          {
            /* NOP */
          case 0:
            Rez0 = Rez0T;
            Rez1 = Rez1T;
            Rez2 = Rez2T;
            Rez3 = Rez3T;
            return;

            /*
              multiply a 4x4 matrix of 16.16 values by a vector of
              16.16 values
            */
          case 1:
            Rez0 = Rez0T;
            Rez1 = Rez1T;
            Rez2 = Rez2T;
            Rez3 = Rez3T;

            Rez0T = (int)((M00 * V0 + M01 * V1 + M02 * V2 + M03 * V3) / 65536.0);
            Rez1T = (int)((M10 * V0 + M11 * V1 + M12 * V2 + M13 * V3) / 65536.0);
            Rez2T = (int)((M20 * V0 + M21 * V1 + M22 * V2 + M23 * V3) / 65536.0);
            Rez3T = (int)((M30 * V0 + M31 * V1 + M32 * V2 + M33 * V3) / 65536.0);
            return;

            /*
              multiply a 3x3 matrix of 16.16 values by a vector of
              16.16 values
            */
          case 2:
            Rez0 = Rez0T;
            Rez1 = Rez1T;
            Rez2 = Rez2T;
            Rez3 = Rez3T;

            Rez0T = (int)((M00 * V0 + M01 * V1 + M02 * V2) / 65536.0);
            Rez1T = (int)((M10 * V0 + M11 * V1 + M12 * V2) / 65536.0);
            Rez2T = (int)((M20 * V0 + M21 * V1 + M22 * V2) / 65536.0);
            return;

            /*
              multiply a 3x3 matrix of 16.16 values by multiple
              vectors, then multiply x and y by n/z

              return the result vectors {x*n/z, y*n/z, z}
            */
          case 3:
            {
              double M;

              Rez0 = Rez0T;
              Rez1 = Rez1T;
              Rez2 = Rez2T;
              Rez3 = Rez3T;

              M = Nfrac16;

              Rez2T = (int32_t)((M20 * V0 + M21 * V1 + M22 * V2) / 65536.0); // z
              if(Rez2T != 0)
                M /= (double)Rez2T;          // n/z

              Rez0T = (int32_t)((M00 * V0 + M01 * V1 + M02 * V2) / 65536.0);
              Rez1T = (int32_t)((M10 * V0 + M11 * V1 + M12 * V2) / 65536.0);

              Rez0T = (double)((Rez0T * M) / 65536.0 / 65536.0); // x * n/z
              Rez1T = (double)((Rez1T * M) / 65536.0 / 65536.0); // y * n/z
            }
            break;

          default:
            break;
          }
      }
      break;

      /* modulo variables */
    case 0x130:
      MADAM.mregs[addr_] = val_;
      MADAM.RMOD = ((val_ & 1) << 7) + ((val_ & 12) << 8) + ((val_ & 0x70) << 4);
      val_ >>= 8;
      MADAM.WMOD = ((val_ & 1) << 7) + ((val_ & 12) << 8) + ((val_ & 0x70) << 4);
      break;

    default:
      MADAM.mregs[addr_] = val_;
      break;
    }
}

static uint32_t OFFSET;
static uint32_t temp1;
static uint32_t Flag;

static double HDDX;
static double HDDY;
static double HDX;
static double HDY;
static double VDX;
static double VDY;
static double XPOS;
static double YPOS;
static double HDX_2;
static double HDY_2;

static int32_t  HDDX1616;
static int32_t  HDDY1616;
static int32_t  HDX1616;
static int32_t  HDY1616;
static int32_t  VDX1616;
static int32_t  VDY1616;
static int32_t  XPOS1616;
static int32_t  YPOS1616;
static int32_t  HDX1616_2;
static int32_t  HDY1616_2;
static uint32_t CEL_ORIGIN_VH_VALUE;
static int8_t   TEXEL_FUN_NUMBER;
static int32_t  TEXTURE_WI_START;
static int32_t  TEXTURE_HI_START;
static int32_t  TEXEL_INCX;
static int32_t  TEXEL_INCY;
static int32_t  TEXTURE_WI_LIM;
static int32_t  TEXTURE_HI_LIM;

static
void
LoadPLUT(uint32_t pnt_,
         int32_t  n_)
{
  int i;

  for(i = 0; i < n_; i++)
    {
#ifdef MSB_FIRST
      MADAM.PLUT[i] = freedo_mem_read16((((pnt_ >> 1) + i)) << 1);
#else
      MADAM.PLUT[i] = freedo_mem_read16((((pnt_ >> 1) + i)^1) << 1);
#endif
    }
}


uint32_t
freedo_madam_cel_handle(void)
{
  CELCYCLES = 0;
  STATBITS |= SPRON;
  Flag = 0;

  while((NEXTSCB != 0) && (!Flag))
    //if(MADAM.FSM==FSM_INPROCESS)
    {
      if((NEXTSCB == 0) || (Flag))
        {
          MADAM.FSM = FSM_IDLE;
          return CELCYCLES;
        }

      //1st step -- parce SCB and load it into registers
      CURRENTSCB = (NEXTSCB & 0x00FFFFFC);
      if((CURRENTSCB >> 20) > 2)
        {
          MADAM.FSM = FSM_IDLE;
          return CELCYCLES;
        }

      printf("SCB: %x\n"
             "flags: %x\n"
             "next: %x\n"
             "data: %x\n"
             "X: %d %d\n"
             "Y: %d %d\n"
             "HDX: %d\n"
             "HDY: %d\n"
             "VDX: %d\n"
             "VDY: %d\n"
             "DDX: %d\n"
             "DDY: %d\n"
             "PPMPC: %x\n"
             "PRE0: %x\n"
             "PRE1: %x\n\n",
             CURRENTSCB,
             mread(CURRENTSCB),
             mread(CURRENTSCB+4),
             mread(CURRENTSCB+8),
             mread(CURRENTSCB+12), mread(CURRENTSCB+12) >> 16,
             mread(CURRENTSCB+16), mread(CURRENTSCB+16) >> 16,
             mread(CURRENTSCB+20),
             mread(CURRENTSCB+24),
             mread(CURRENTSCB+28),
             mread(CURRENTSCB+32),
             mread(CURRENTSCB+36),
             mread(CURRENTSCB+40),
             mread(CURRENTSCB+44),
             mread(CURRENTSCB+48),
             mread(CURRENTSCB+52));

      OFFSET      = CURRENTSCB;
      SCBFLAGS    = mread(CURRENTSCB);
      CURRENTSCB += 4;

      if(SCBFLAGS & SCB_PXOR)
        {
          PXOR1 = 0;
          PXOR2 = 0x1F1F1F1F;
        }
      else
        {
          PXOR1 = 0xFFFFFFFF;
          PXOR2 = 0;
        }

      Flag  = 0;
      PLUTF = 0;
      PDATF = 0;
      NSCBF = 0;

      NEXTSCB = mread(CURRENTSCB) & (~3);

      if(!(SCBFLAGS & SCB_NPABS))
        {
          NEXTSCB += CURRENTSCB + 4;
          NEXTSCB &= 0x00FFFFFF;
        }

      if(NEXTSCB == 0)
        NSCBF = 1;
      if((NEXTSCB >> 20) > 2)
        NSCBF = 1;

      CURRENTSCB += 4;

      PDATA = mread(CURRENTSCB) & (~3);

      switch(PDATA)
        {
        case 0x139630:
          break;
        }

      /*
        if((PDATA==0))
      	PDATF=1;
      */
      if(!(SCBFLAGS & SCB_SPABS))
        {
          PDATA += CURRENTSCB + 4;
          PDATA &= 0x00FFFFFF;
        }

      if((PDATA >> 20) > 2)
        PDATF = 1;
      CURRENTSCB += 4;

      if(SCBFLAGS & SCB_LDPLUT)
        {
          PLUTDATA = mread(CURRENTSCB) & (~3);
          /*
            if((PLUTDATA == 0))
              PLUTF=1;
          */
          if(!(SCBFLAGS & SCB_PPABS))
            {
              PLUTDATA += CURRENTSCB + 4;
              PLUTDATA &= 0x00FFFFFF;
            }

          if((PLUTDATA >> 20) > 2)
            PLUTF = 1;
        }

      CURRENTSCB += 4;

      if(NSCBF)
        SCBFLAGS |= SCB_LAST;

      if(SCBFLAGS & SCB_LAST)
        Flag = 1;

      if(SCBFLAGS & SCB_YOXY)
        {
          XPOS1616    = mread(CURRENTSCB);
          XPOS        = XPOS1616 / 65536.0;
          CURRENTSCB += 4;
          YPOS1616    = mread(CURRENTSCB);
          YPOS        = YPOS1616 / 65536.0;
          CURRENTSCB += 4;
        }
      else
        {
          CURRENTSCB += 8;
        }

      /*
        Get the VH value for this cel. This is done in case the
        cel later decides to use the position as the source of
        its VH values in the projector.
      */
      CEL_ORIGIN_VH_VALUE = ((XPOS1616 & 0x1) | ((YPOS1616 & 0x1) << 15));

      /*
        if((SCBFLAGS&SCB_SKIP)&& debug)
        printf("###Cel skipped!!! PDATF=%d PLUTF=%d NSCBF=%d\n",PDATF,PLUTF,NSCBF);
      */

      if(SCBFLAGS & SCB_LAST)
        NEXTSCB = 0;

      if(SCBFLAGS & SCB_LDSIZE)
        {
          HDX1616     = ((int32_t)mread(CURRENTSCB)) >> 4;
          HDX         = HDX1616 / 65536.0;
          CURRENTSCB += 4;
          HDY1616     = ((int32_t)mread(CURRENTSCB)) >> 4;
          HDY         = HDY1616 / 65536.0;
          CURRENTSCB += 4;
          VDX1616     = mread(CURRENTSCB);
          VDX         = VDX1616 / 65536.0;
          CURRENTSCB += 4;
          VDY1616     = mread(CURRENTSCB);
          VDY         = VDY1616 / 65536.0;
          CURRENTSCB += 4;
        }

      if(SCBFLAGS & SCB_LDPRS)
        {
          HDDX1616    = ((int32_t)mread(CURRENTSCB)) >> 4;
          HDDX        = HDDX1616 / 65536.0;
          CURRENTSCB += 4;
          HDDY1616    = ((int32_t)mread(CURRENTSCB)) >> 4;
          HDDY        = HDDY1616 / 65536.0;
          CURRENTSCB += 4;
        }

      if(SCBFLAGS & SCB_LDPPMP)
        {
          PIXC        = mread(CURRENTSCB);
          CURRENTSCB += 4;
        }

      if(SCBFLAGS & SCB_SCBPRE)
        {
          PRE0        = mread(CURRENTSCB);
          CURRENTSCB += 4;
          if(!(SCBFLAGS & SCB_PACKED))
            {
              PRE1        = mread(CURRENTSCB);
              CURRENTSCB += 4;
            }
        }
      else if(!PDATF)
        {
          PRE0   = mread(PDATA);
          PDATA += 4;
          if(!(SCBFLAGS & SCB_PACKED))
            {
              PRE1   = mread(PDATA);
              PDATA += 4;
            }
        }

      /* PDEC data compute */
      {
        /* pdec.mode = PRE0 & PRE0_BPP_MASK; */
        switch(PRE0 & PRE0_BPP_MASK)
          {
          case 0:
          case 7:
            continue;
          case 1:
            pdec.plutaSCBbits  = ((SCBFLAGS & 0x0F) * 4);
            pdec.pixelBitsMask = 1; /* 1 bit */
            break;
          case 2:
            pdec.plutaSCBbits  = ((SCBFLAGS & 0x0E) * 4);
            pdec.pixelBitsMask = 3; /* 2 bit */
            break;
          case 3:
          default:
            pdec.plutaSCBbits  = ((SCBFLAGS & 0x08) * 4);
            pdec.pixelBitsMask = 15; /* 4 bit */
            break;
          }

        pdec.tmask = !(SCBFLAGS & SCB_BGND);

        pproj.pmode        = (SCBFLAGS & SCB_POVER_MASK);
        pproj.pmodeORmask  = ((pproj.pmode == PMODE_ONE ) ? 0x8000 : 0x0000);
        pproj.pmodeANDmask = ((pproj.pmode != PMODE_ZERO) ? 0xFFFF : 0x7FFF);
      }

      /* load PLUT */
      if((SCBFLAGS & SCB_LDPLUT) && !PLUTF)
        {
          switch(PRE0 & PRE0_BPP_MASK)
            {
            case 1:
              LoadPLUT(PLUTDATA,2);
              break;
            case 2:
              LoadPLUT(PLUTDATA,4);
              break;
            case 3:
              LoadPLUT(PLUTDATA,16);
              break;
            default:
              LoadPLUT(PLUTDATA,32);
            };
        }

      /*
        SCB decoded -- let's print out our current status
        step#2 -- getting CEL data
      */

      if(!(SCBFLAGS & SCB_SKIP) && !PDATF)
        {
          if(SCBFLAGS & SCB_PACKED)
            {
              DrawPackedCel_New();
            }
          else
            {
              if((PRE1 & PRE1_LRFORM) && (BPP[PRE0 & PRE0_BPP_MASK] == 16))
                DrawLRCel_New();
              else
                DrawLiteralCel_New();
            }

        }
    }

  /* STATBITS &= ~SPRON; */
  if((NEXTSCB == 0) || (Flag))
    MADAM.FSM = FSM_IDLE;

  printf("END\n");

  return CELCYCLES;
}

static
void
HandleDMA8(void)
{
  /* pbus transfer */
  if(MADAM.mregs[0x8] & 0x8000)
    {
      DMAPBus();
      MADAM.mregs[0x8] &= ~0x8000; /* dma done */
      freedo_clio_fiq_generate(0,1);
    }
}

static
void
DMAPBus(void)
{
  uint32_t  i;
  uint32_t *pbus_buf;
  int32_t   pbus_size;

  if((int32_t)MADAM.mregs[0x574] < 0)
    return;

  MADAM.mregs[0x574] -= 4;
  MADAM.mregs[0x570] += 4;
  MADAM.mregs[0x578] += 4;

  i = 0;
  pbus_buf = freedo_pbus_buf();
  pbus_size = freedo_pbus_size();
  while(((int32_t)MADAM.mregs[0x574] > 0) && (pbus_size > 0))
    {
      freedo_io_write(MADAM.mregs[0x570],pbus_buf[i]);
      MADAM.mregs[0x574] -= 4;
      pbus_size          -= 4;
      MADAM.mregs[0x570] += 4;
      MADAM.mregs[0x578] += 4;
      i++;
    }

  while((int32_t)MADAM.mregs[0x574] > 0)
    {
      freedo_io_write(MADAM.mregs[0x570],0xFFFFFFFF);
      MADAM.mregs[0x574] -= 4;
      MADAM.mregs[0x570] += 4;
      MADAM.mregs[0x578] += 4;
    }

  MADAM.mregs[0x574] = 0xFFFFFFFC;
}

void
freedo_madam_init(uint8_t *mem_)
{
  int32_t i;
  int32_t j;
  int32_t n;

  ADD       = 0;
  USECEL    = 1;
  CELCYCLES = 0;
  Mem       = mem_;

  bitoper.bitset = 1;

  MADAM.FSM = FSM_IDLE;

  for(i = 0; i < MADAM_REGISTER_COUNT; i++)
    MADAM.mregs[i] = 0;

  /* DRAM dux init */
  MADAM.mregs[4]   = 0x29;
  MADAM.mregs[574] = 0xFFFFFFFC;

#if 1
  MADAM.mregs[0] = 0x01020000;        /* for Green matrix engine autodetect */
  /* MADAM.mregs[0] = 0x02022000; */  /* for Green matrix engine autodetect */
#else
  MADAM.mregs[0] = 0x01020001;        /* for ARM soft emu of matrix engine */
#endif

  for(i = 0; i < 32; i++)
    for(j = 0; j < 8; j++)
      for(n = 0; n < 4; n++)
        PSCALAR[j][n][i] = ((i * (j + 1)) >> PDV(n));

  for(i = 0; i < 256; i++)
    {
      pdeco_t  pix1;
      pdeco_t  pix2;
      uint16_t pres;
      uint16_t resamv;

      pix1.raw     = i;
      pix2.r16b.b  = (pix1.u8b.b << 3) + (pix1.u8b.b << 1) + (pix1.u8b.b >> 1);
      pix2.r16b.g  = (pix1.u8b.g << 2) + (pix1.u8b.g >> 1);
      pix2.r16b.r  = (pix1.u8b.r << 2) + (pix1.u8b.r >> 1);
      pres         = pix2.raw;
      pres        &= 0x7fff;
      MAPu8b[i]    = pres;

      resamv       = ((pix1.c8b.m << 1) + pix1.c8b.mpw);
      resamv       = ((resamv << 6) + (resamv << 3) + resamv);
      MAPc8bAMV[i] = resamv;
    }

  for(i = 0; i < (8*8*8); i++)
    {
      pdeco_t pix1;

      pix1.raw = i << 5;
      MAPc16bAMV[i] = ((pix1.c16b.mr << 6) + (pix1.c16b.mg << 3) + pix1.c16b.mb);
    }
}

static
INLINE
uint32_t
mread(uint32_t addr_)
{
  uint32_t val;

  val = freedo_mem_read32(addr_);
  CELCYCLES += 1;

  return val;
}

static
INLINE
void
mwrite(const uint32_t addr_,
       const uint32_t val_)
{
  freedo_mem_write32(addr_,val_);
  CELCYCLES += 2;
}

static
INLINE
void
mwriteh(const uint32_t addr_,
        const uint16_t val_)
{
  CELCYCLES += 2;
#ifdef MSB_FIRST
  freedo_mem_write16(addr_,val_);
#else
  freedo_mem_write16((addr_ ^ 2),val_);
#endif
}

static
INLINE
uint16_t
mreadh(const uint32_t addr_)
{
  CELCYCLES += 1;
#ifdef MSB_FIRST
  return freedo_mem_read16(addr_);
#else
  return freedo_mem_read16(addr_ ^ 2);
#endif
}

static
uint32_t
PDEC(const uint32_t  pixel_,
     uint16_t       *amv_)
{
  pdeco_t pix1;
  uint16_t resamv;
  uint16_t pres;

  pix1.raw = pixel_;

  switch(PRE0 & PRE0_BPP_MASK)
    {
    default:
    case 1: /* 1 bit  */
    case 2: /* 2 bits */
    case 3: /* 4 bits */
      pres   = MADAM.PLUT[(pdec.plutaSCBbits + ((pix1.raw & pdec.pixelBitsMask) * 2)) >> 1];
      resamv = 0x49;
      break;

    case 4:   /* 6 bits */
      /* pmode = pix1.c6b.pw; ??? */
      pres   = MADAM.PLUT[pix1.c6b.c];
      pres   = (pres & 0x7FFF) + (pix1.c6b.pw << 15);
      resamv = 0x49;
      break;

    case 5:   /* 8 bits */
      if(PRE0 & PRE0_LINEAR)    /* uncoded 8 bit CEL */
        {
          pres   = MAPu8b[pix1.raw & 0xFF];
          resamv = 0x49;
        }
      else                      /* coded 8 bit CEL */
        {
          pres   = MADAM.PLUT[pix1.c8b.c];
          resamv = MAPc8bAMV[pix1.raw & 0xFF];
        }
      break;

    case 6:  /* 16 bits */
    case 7:
      if(PRE0 & PRE0_LINEAR)    /* uncoded 16 bit CEL */
        {
          pres   = pix1.raw;
          resamv = 0x49;
        }
      else                      /* coded 16 bit CEL */
        {
          pres   = MADAM.PLUT[pix1.c16b.c];
          pres   = ((pres & 0x7FFF) | (pixel_ & 0x8000));
          resamv = MAPc16bAMV[(pix1.raw >> 5) & 0x1FF];
        }

      break;
    }

  *amv_ = resamv;

  /* conceptual end of decoder */

  /*
    TODO: Do PROJECTOR functions now?
    They'll be done before using the PROCESSOR.

    if(!(PRE1&PRE1_NOSWAP) && (SCBCTL0&(1<<27)))
      pres = (pres&0x7ffe)|((pres&0x8000)>>15)|((pres&1)<<15);

    if(!(SCBCTL0 & 0x80000000))
      pres = (pres&0x7fff)|((SCBCTL0>>15)&0x8000);

    pres=(pres|pdec.pmodeORmask)&pdec.pmodeANDmask;
  */

  pproj.Transparent = (((pres & 0x7FFF) == 0x0) & pdec.tmask);

  return pres;
}

static
uint32_t
PPROJ_OUTPUT(uint32_t pdec_output_,
             uint32_t pproc_output_,
             uint32_t pframe_input_)
{
  int32_t  b15mode;
  int32_t  b0mode;
  uint32_t VHOutput;

  /*
    SCB_PLUTPOS flag
    Determine projector's originating source of VH values.
  */

  if(SCBFLAGS & SCB_PLUTPOS) /* Use pixel decoder output. */
    VHOutput = (pdec_output_ & 0x8001);
  else /* Use VH values determined from the CEL's origin. */
    VHOutput = CEL_ORIGIN_VH_VALUE;

  /*
    SWAPHV flag
    Swap the H and V values now if requested.
  */
  if(SCBCTL0 & SWAPHV)
    {
      /* TODO: I have read that PRE1 is only set for unpacked CELs.
         So... should this be ignored if using packed CELs? I don't
         know. */
      if(!(PRE1 & PRE1_NOSWAP))
        VHOutput = ((VHOutput >> 15) | ((VHOutput & 1) << 15));
    }

#if 0
  /*
    CFBDSUB flag
    Substitute the VH values from the frame buffer if requested.
  */
  if(SCBCTL0 & CFBDSUB)
    {
      /* TODO: This should be re-enabled sometime. However, it currently
         causes the wing commander 3 movies to screw up again! There
         must be some missing mbehavior elsewhere. */
      VHOutput = (pframe_input & 0x8001);
    }
#endif

  /*
    B15POS_MASK settings
    Substitute the V value explicitly if requested.
  */
  b15mode = (SCBCTL0 & B15POS_MASK);
  switch(b15mode)
    {
    case B15POS_PDC:
    default:
      /* do nothing */
      break;
    case B15POS_0:
      VHOutput = (VHOutput & ~0x8000);
      break;
    case B15POS_1:
      VHOutput |= 0x8000;
      break;
    }

  /*
    B15POS_MASK settings
    Substitute the H value explicitly if requested.
  */
  b0mode = (SCBCTL0 & B0POS_MASK);
  switch(b0mode)
    {
    case B0POS_PDC:
      /* do nothing  */
      break;
    case B0POS_PPMP:
      /* Use LSB from pixel processor output. */
      VHOutput = ((VHOutput & ~0x1) | (pproc_output_ & 0x1));
      break;
    case B0POS_0:
      VHOutput = (VHOutput & ~0x1);
      break;
    case B0POS_1:
      VHOutput |= 0x01;
      break;
    }

  return ((pproc_output_ & 0x7FFE) | VHOutput);
}

static
FORCEINLINE
int8_t
clamp_i8(const int8_t x_,
         const int8_t min_,
         const int8_t max_)
{
  return ((x_ < min_) ?
          min_ :
          ((x_ > max_) ? max_ : x_));
}

static
uint32_t
PPROC(uint32_t pixel_,
      uint32_t fpix_,
      uint32_t amv_)
{
  AVS_t AV;
  PXC_t pixc;

  pdeco_t input1;
  pdeco_t out;
  pdeco_t pix1;

#pragma pack(push,1)
  union
  {
    uint32_t raw;
    struct
    {
      int8_t R;
      int8_t B;
      int8_t G;
      int8_t a;
    };
  } color1, color2, AOP, BOP;
#pragma pack(pop)

  /*
    Set PMODE according to the values set up in the SCBFLAGS word.
    (This merely uses masks here because it's faster).
    This is a duty of the PROJECTOR, but we'll do it here because its
    easier.
  */
  pixel_ = ((pixel_ | pproj.pmodeORmask) & pproj.pmodeANDmask);

  pixc.raw = (PIXC & 0xFFFF);
  if(pixel_ & 0x8000)
    pixc.raw = (PIXC >> 16);

  /*
    now let's select the sources
    1. av
    2. input1
    3. input2
    pixc.raw = 0;
  */

  if(SCBFLAGS & SCB_USEAV)
    {
      AV.raw = pixc.meaning.av;
    }
  else
    {
      AV.avsignal.dv3   = 0;
      AV.avsignal.nCLIP = 0;
      AV.avsignal.XTEND = 0;
      AV.avsignal.NEG   = 0;
    }

  if(!pixc.meaning.s1)
    input1.raw = pixel_;
  else
    input1.raw = fpix_;

  switch(pixc.meaning.s2)
    {
    case 0:
      color2.raw = 0;
      break;
    case 1:
      color2.R = color2.G = color2.B = (pixc.meaning.av >> AV.avsignal.dv3);
      break;
    case 2:
      pix1.raw = fpix_;
      color2.R = ((pix1.r16b.r) >> AV.avsignal.dv3);
      color2.G = ((pix1.r16b.g) >> AV.avsignal.dv3);
      color2.B = ((pix1.r16b.b) >> AV.avsignal.dv3);
      break;
    case 3:
      pix1.raw = pixel_;
      color2.R = (pix1.r16b.r >> AV.avsignal.dv3);
      color2.G = (pix1.r16b.g >> AV.avsignal.dv3);
      color2.B = (pix1.r16b.b >> AV.avsignal.dv3);
      break;
    }

  switch(pixc.meaning.ms)
    {
    case 0:
      color1.R = PSCALAR[pixc.meaning.mxf][pixc.meaning.dv1][input1.r16b.r];
      color1.G = PSCALAR[pixc.meaning.mxf][pixc.meaning.dv1][input1.r16b.g];
      color1.B = PSCALAR[pixc.meaning.mxf][pixc.meaning.dv1][input1.r16b.b];
      break;
    case 1:
      color1.R = PSCALAR[(amv_ >> 6) & 7][pixc.meaning.dv1][input1.r16b.r];
      color1.G = PSCALAR[(amv_ >> 3) & 7][pixc.meaning.dv1][input1.r16b.g];
      color1.B = PSCALAR[(amv_ >> 0) & 7][pixc.meaning.dv1][input1.r16b.b];
      break;
    case 2:
      pix1.raw = pixel_;
      color1.R = PSCALAR[pix1.r16b.r >> 2][pix1.r16b.r & 3][input1.r16b.r];
      color1.G = PSCALAR[pix1.r16b.g >> 2][pix1.r16b.g & 3][input1.r16b.g];
      color1.B = PSCALAR[pix1.r16b.b >> 2][pix1.r16b.b & 3][input1.r16b.b];
      break;
    case 3:
      color1.R = PSCALAR[4][pixc.meaning.dv1][input1.r16b.r];
      color1.G = PSCALAR[4][pixc.meaning.dv1][input1.r16b.g];
      color1.B = PSCALAR[4][pixc.meaning.dv1][input1.r16b.b];
      break;
    }

  /* Use this to render magenta for testing. */
#if 0
  {
    pdeco_t magenta;

    magenta.r16b.r = 0x1b;
    magenta.r16b.g = 0x00;
    magenta.r16b.b = 0x1b;
    magenta.r16b.p = 1;

    return magenta.raw;
  }
#endif

  /*
    ok -- we got the sources -- now RGB processing
  */

  /* AOP/BOP calculation */
  AOP.raw     = (color1.raw & PXOR1);
  color1.raw &= PXOR2;

  if(AV.avsignal.NEG)
    BOP.raw = (color2.raw ^ 0x00FFFFFF);
  else
    BOP.raw = (color2.raw ^ color1.raw);

  if(AV.avsignal.XTEND)
    {
      BOP.R = ((BOP.R << 3) >> 3);
      BOP.B = ((BOP.B << 3) >> 3);
      BOP.G = ((BOP.G << 3) >> 3);
    }

  color2.R = ((AOP.R + BOP.R + AV.avsignal.NEG) >> pixc.meaning.dv2);
  color2.G = ((AOP.G + BOP.G + AV.avsignal.NEG) >> pixc.meaning.dv2);
  color2.B = ((AOP.B + BOP.B + AV.avsignal.NEG) >> pixc.meaning.dv2);

  if(!AV.avsignal.nCLIP)
    {
      color2.R = clamp_i8(color2.R,0,31);
      color2.G = clamp_i8(color2.G,0,31);
      color2.B = clamp_i8(color2.B,0,31);
    }

  out.raw    = 0;
  out.r16b.r = color2.R;
  out.r16b.g = color2.G;
  out.r16b.b = color2.B;

  /* TODO: Is this something the PROJECTOR should do? */
  if(!(SCBFLAGS & SCB_NOBLK) && (out.raw == 0))
    out.raw = (1 << 10);

  /*
    if(!(PRE1 & PRE1_NOSWAP) && (SCBCTL0 & (1 << 27)))
    out.raw = ((out.raw & 0x7FFE) |
    ((out.raw & 0x8000) >> 15) |
    ((out.raw & 1) << 15));
  */

  /*
    if(!(SCBCTL0 & 0x80000000))
    out.raw = ((out.raw & 0x7FFF) | ((SCBCTL0 >> 15) & 0x8000));
  */

  return out.raw;
}


uint32_t*
freedo_madam_registers(void)
{
  return MADAM.mregs;
}

static
void
DrawPackedCel_New(void)
{
  uint16_t CURPIX;
  uint16_t LAMV;

  int32_t lastaddr;
  int32_t xcur;
  int32_t ycur;
  int32_t xvert;
  int32_t yvert;
  int32_t xdown;
  int32_t ydown;
  int32_t hdx;
  int32_t hdy;

  uint32_t start = PDATA;

  nrows = ((PRE0 & PRE0_VCNT_MASK) >> PRE0_VCNT_SHIFT);

  bpp = BPP[PRE0 & PRE0_BPP_MASK];

  offsetl = ((bpp < 8) ? 1 : 2);

  pixcount = 0;
  compsize = 30;

  SPRHI = nrows + 1;
  calcx = 0;

  if(TestInitVisual(1))
    return;

  xvert = XPOS1616;
  yvert = YPOS1616;

  if(TEXEL_FUN_NUMBER == 0)
    {
      for(currentrow = 0; currentrow < TEXTURE_HI_LIM; currentrow++)
        {
          int wcnt;
          int scipw;

          BitReaderBig_AttachBuffer(&bitoper,start);
          offset = BitReaderBig_Read(&bitoper,(offsetl << 3));

          /* BITCALC=((offset+2)<<2)<<5; */
          lastaddr  = (start + ((offset + 2) << 2));
          eor       = 0;
          xcur      = xvert;
          ycur      = yvert;
          xvert    += VDX1616;
          yvert    += VDY1616;

          if(TEXTURE_HI_START)
            {
              TEXTURE_HI_START--;
              start = lastaddr;
              continue;
            }

          scipw = TEXTURE_WI_START;
          wcnt  = scipw;

          /* while not end of row */
          while(!eor)
            {
              type = BitReaderBig_Read(&bitoper,2);
              if((int32_t)(bitoper.point + start) >= (lastaddr))
                type = 0;

              pixcount = BitReaderBig_Read(&bitoper,6) + 1;

              if(scipw)
                {
                  if(type == 0)
                    break;
                  if(scipw >= (int32_t)(pixcount))
                    {
                      scipw -= (pixcount);
                      if(HDX1616)
                        xcur += (HDX1616 * pixcount);
                      if(HDY1616)
                        ycur += (HDY1616 * pixcount);
                      if(type == 1)
                        BitReaderBig_Skip(&bitoper,bpp*pixcount);
                      else if(type == 3)
                        BitReaderBig_Skip(&bitoper,bpp);
                      continue;
                    }
                  else
                    {
                      if(HDX1616)
                        xcur += (HDX1616 * scipw);
                      if(HDY1616)
                        ycur += (HDY1616 * scipw);
                      pixcount -= scipw;
                      if(type == 1)
                        BitReaderBig_Skip(&bitoper,bpp*scipw);
                      scipw = 0;
                    }
                }

              /*
                if(wcnt >= TEXTURE_WI_LIM)
                break;
              */
              wcnt += pixcount;
              if(wcnt > TEXTURE_WI_LIM)
                {
                  pixcount -= (wcnt - TEXTURE_WI_LIM);
                  /*
                    if(pixcount >> 31)
                    break;
                  */
                }

              switch(type)
                {
                case 0: /* end of row */
                  eor = 1;
                  break;
                case 1: /* PACK_LITERAL */

                  for(pix = 0; pix < pixcount; pix++)
                    {
                      CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);
                      if(!pproj.Transparent)
                        {
                          uint32_t pixel;
                          uint32_t framePixel;

                          framePixel = mreadh((PIXSOURCE + XY2OFF(xcur >> 16,ycur >> 16,MADAM.RMOD)));
                          pixel      = PPROC(CURPIX,framePixel,LAMV);
                          pixel      = PPROJ_OUTPUT(CURPIX,pixel,framePixel);
                          int32_t foo = xcur >> 16;
                          if(PDATA == 0x139630)
                            {
                              foo -= 12;
                            }
                          mwriteh((FBTARGET + XY2OFF(foo,ycur >> 16,MADAM.WMOD)),pixel);
                        }

                      xcur += HDX1616;
                      ycur += HDY1616;
                    }
                  break;
                case 2: /* PACK_TRANSPARENT */
                  /* calcx += (pixcount + 1); */
                  if(HDX1616)
                    xcur += (HDX1616 * pixcount);
                  if(HDY1616)
                    ycur += (HDY1616 * pixcount);
                  break;
                case 3: /* PACK_REPEAT */
                  CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                  if(!pproj.Transparent)
                    TexelDraw_Line(CURPIX,LAMV,xcur,ycur,pixcount);

                  if(HDX1616)
                    xcur += (HDX1616 * pixcount);
                  if(HDY1616)
                    ycur += (HDY1616 * pixcount);
                  break;
                }

              if(wcnt >= TEXTURE_WI_LIM)
                break;
            }

          start = lastaddr;
        }
    }
  else if(TEXEL_FUN_NUMBER == 1)
    {
      int drawHeight;

      drawHeight = VDY1616;
      if((SCBFLAGS & SCB_MARIA) && (drawHeight > (1 << 16)))
        drawHeight = (1 << 16);

      for(currentrow = 0; currentrow < SPRHI; currentrow++)
        {
          BitReaderBig_AttachBuffer(&bitoper,start);
          offset = BitReaderBig_Read(&bitoper,(offsetl << 3));

          BITCALC = (((offset + 2) << 2) << 5);
          lastaddr = (start + ((offset + 2) << 2));

          eor = 0;

          xcur   = xvert;
          ycur   = yvert;
          xvert += VDX1616;
          yvert += VDY1616;

          /* while not end of row */
          while(!eor)
            {
              int32_t __pix;

              type = BitReaderBig_Read(&bitoper,2);
              if((bitoper.point + start) >= lastaddr)
                type = 0;

              __pix = (BitReaderBig_Read(&bitoper,6) + 1);

              switch(type)
                {
                case 0: /* end of row */
                  eor = 1;
                  break;
                case 1: /* PACK_LITERAL */
                  while(__pix)
                    {
                      __pix--;
                      CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                      if(!pproj.Transparent)
                        {
                          if(TexelDraw_Scale(CURPIX,
                                             LAMV,
                                             xcur >> 16,
                                             ycur >> 16,
                                             ((xcur + (HDX1616 + VDX1616)) >> 16),
                                             ((ycur + (HDY1616 + drawHeight)) >> 16)))
                            break;
                        }

                      xcur += HDX1616;
                      ycur += HDY1616;
                    }
                  break;
                case 2: /* PACK_TRANSPARENT */
                  /* calcx += (pixcount + 1); */
                  xcur  += (HDX1616 * __pix);
                  ycur  += (HDY1616 * __pix);
                  __pix  = 0;
                  break;
                case 3: /* PACK_REPEAT */
                  CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);
                  if(!pproj.Transparent)
                    {
                      if(TexelDraw_Scale(CURPIX,
                                         LAMV,
                                         xcur >> 16,
                                         ycur >> 16,
                                         ((xcur + (HDX1616 * __pix) + VDX1616) >> 16),
                                         ((ycur + (HDY1616 * __pix) + drawHeight) >> 16)))
                        break;

                    }

                  xcur += (HDX1616 * __pix);
                  ycur += (HDY1616 * __pix);
                  __pix = 0;
                  break;
                }

              if(__pix)
                break;
            }

          start = lastaddr;
        }
    }
  else
    {
      for(currentrow = 0; currentrow < SPRHI; currentrow++)
        {
          BitReaderBig_AttachBuffer(&bitoper,start);
          offset = BitReaderBig_Read(&bitoper,(offsetl << 3));

          BITCALC  = (((offset + 2) << 2) << 5);
          lastaddr = (start + ((offset + 2) << 2));

          eor = 0;

          xcur = xvert;
          ycur = yvert;
          hdx  = HDX1616;
          hdy  = HDY1616;

          xvert   += VDX1616;
          yvert   += VDY1616;
          HDX1616 += HDDX1616;
          HDY1616 += HDDY1616;

          xdown = xvert;
          ydown = yvert;

          /* while not end of row */
          while(!eor)
            {
              int32_t __pix;

              type = BitReaderBig_Read(&bitoper,2);
              if((bitoper.point + start) >= lastaddr)
                type = 0;

              __pix = (BitReaderBig_Read(&bitoper,6) + 1);

              switch(type)
                {
                case 0: /* end of row */
                  eor = 1;
                  break;
                case 1: /* PACK_LITERAL */
                  while(__pix)
                    {
                      CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);
                      __pix--;

                      if(!pproj.Transparent)
                        {
                          if(TexelDraw_Arbitrary(CURPIX,LAMV,xcur,ycur,xcur+hdx,ycur+hdy,xdown+HDX1616,ydown+HDY1616,xdown,ydown))
                            break;
                        }

                      xcur  += hdx;
                      ycur  += hdy;
                      xdown += HDX1616;
                      ydown += HDY1616;
                    }
                  break;
                case 2: /* PACK_TRANSPARENT */
                  /* calcx += (pixcount + 1); */
                  xcur  += (hdx * __pix);
                  ycur  += (hdy * __pix);
                  xdown += (HDX1616 * __pix);
                  ydown += (HDY1616 * __pix);
                  __pix  = 0;
                  break;
                case 3: /* PACK_REPEAT */
                  CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                  if(!pproj.Transparent)
                    {
                      while(__pix)
                        {
                          __pix--;
                          if(TexelDraw_Arbitrary(CURPIX,LAMV,xcur,ycur,xcur+hdx,ycur+hdy,xdown+HDX1616,ydown+HDY1616,xdown,ydown))
                            break;
                          xcur  += hdx;
                          ycur  += hdy;
                          xdown += HDX1616;
                          ydown += HDY1616;
                        }
                    }
                  else
                    {
                      xcur  += (hdx * __pix);
                      ycur  += (hdy * __pix);
                      xdown += (HDX1616 * __pix);
                      ydown += (HDY1616 * __pix);
                      __pix  = 0;
                    }
                  break;
                }

              if(__pix)
                break;
            }

          start = lastaddr;
        }
    }

  SPRWI++;

  if(FIXMODE & FIX_BIT_GRAPHICS_STEP_Y)
    {
      YPOS1616 = ycur;
      YPOS     = (YPOS1616 / 65536.0);
    }
  else
    {
      XPOS1616 = xcur;
      XPOS     = (XPOS1616 / 65536.0);
    }
}

static
void
DrawLiteralCel_New(void)
{
  int32_t xcur;
  int32_t ycur;
  int32_t xvert;
  int32_t yvert;
  int32_t xdown;
  int32_t ydown;
  int32_t hdx;
  int32_t hdy;
  uint16_t CURPIX;
  uint16_t LAMV;

  bpp      = BPP[PRE0 & PRE0_BPP_MASK];
  offsetl  = ((bpp < 8) ? 1 : 2);
  pixcount = 0;
  offset   = ((offsetl == 1) ?
              ((PRE1 & PRE1_WOFFSET8_MASK) >> PRE1_WOFFSET8_SHIFT):
              ((PRE1 & PRE1_WOFFSET10_MASK) >> PRE1_WOFFSET10_SHIFT));

  SPRWI = (1 + (PRE1 & PRE1_TLHPCNT_MASK));
  SPRHI = (1 + ((PRE0 & PRE0_VCNT_MASK) >> PRE0_VCNT_SHIFT));

  if(TestInitVisual(0))
    return;

  xvert = XPOS1616;
  yvert = YPOS1616;

  switch(TEXEL_FUN_NUMBER)
    {
    case 0:
      {
        uint32_t i;

        SPRWI -= ((PRE0 >> 24) & 0xF);
        xvert += (TEXTURE_HI_START * VDX1616);
        yvert += (TEXTURE_HI_START * VDY1616);
        PDATA += (((offset + 2) << 2) * TEXTURE_HI_START);

        if(SPRWI > TEXTURE_WI_LIM)
          SPRWI = TEXTURE_WI_LIM;

        for(i = TEXTURE_HI_START; i < TEXTURE_HI_LIM; i++)
          {
            uint32_t j;

            BitReaderBig_AttachBuffer(&bitoper,PDATA);
            BITCALC = (((offset + 2) << 2) << 5);
            xcur = (xvert + TEXTURE_WI_START * HDX1616);
            ycur = (yvert + TEXTURE_WI_START * HDY1616);
            BitReaderBig_Skip(&bitoper,(bpp * (((PRE0 >> 24) & 0xF))));
            if(TEXTURE_WI_START)
              BitReaderBig_Skip(&bitoper,(bpp * TEXTURE_WI_START));

            xvert += VDX1616;
            yvert += VDY1616;

            for(j = TEXTURE_WI_START; j < SPRWI; j++)
              {
                CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                if(!pproj.Transparent)
                  {
                    uint32_t framePixel;
                    uint32_t pixel;

                    framePixel = mreadh((PIXSOURCE + XY2OFF(xcur >> 16,ycur >> 16,MADAM.RMOD)));
                    pixel      = PPROC(CURPIX,framePixel,LAMV);
                    pixel      = PPROJ_OUTPUT(CURPIX,pixel,framePixel);
                    mwriteh((FBTARGET + XY2OFF(xcur >> 16,ycur >> 16,MADAM.WMOD)),pixel);
                  }

                xcur += HDX1616;
                ycur += HDY1616;
              }

            PDATA += ((offset+2) << 2);
          }
      }
      break;
    case 1:
      {
        int32_t drawHeight;
        uint32_t i;
        uint32_t j;

        SPRWI -= ((PRE0 >> 24) & 0xF);

        drawHeight = VDY1616;
        if((SCBFLAGS & SCB_MARIA) && (drawHeight > (1 << 16)))
          drawHeight = (1 << 16);

        for(i = 0; i < SPRHI; i++)
          {
            BitReaderBig_AttachBuffer(&bitoper,PDATA);
            BITCALC = (((offset + 2) << 2) << 5);
            xcur   = xvert;
            ycur   = yvert;
            xvert += VDX1616;
            yvert += VDY1616;
            BitReaderBig_Skip(&bitoper,(bpp * (((PRE0 >> 24) & 0xF))));

            for(j = 0; j < SPRWI; j++)
              {
                CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                if(!pproj.Transparent)
                  {
                    if(TexelDraw_Scale(CURPIX,
                                       LAMV,
                                       xcur >> 16,
                                       ycur >> 16,
                                       ((xcur + HDX1616 + VDX1616) >> 16),
                                       ((ycur + HDY1616 + drawHeight) >> 16)))
                      break;
                  }

                xcur += HDX1616;
                ycur += HDY1616;
              }

            PDATA += ((offset + 2) << 2);
          }
      }
      break;
    default:
      {
        uint32_t i;
        uint32_t j;

        SPRWI -= ((PRE0 >> 24) & 0xF);
        for(i = 0; i < SPRHI; i++)
          {
            BitReaderBig_AttachBuffer(&bitoper,PDATA);
            BITCALC = (((offset + 2) << 2) << 5);

            xcur = xvert;
            ycur = yvert;
            hdx  = HDX1616;
            hdy  = HDY1616;

            xvert   += VDX1616;
            yvert   += VDY1616;
            HDX1616 += HDDX1616;
            HDY1616 += HDDY1616;

            BitReaderBig_Skip(&bitoper,(bpp * (((PRE0 >> 24) & 0xF))));

            xdown = xvert;
            ydown = yvert;

            for(j = 0; j < SPRWI; j++)
              {
                CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                if(!pproj.Transparent)
                  {
                    if(TexelDraw_Arbitrary(CURPIX, LAMV, xcur, ycur, xcur+hdx, ycur+hdy, xdown+HDX1616, ydown+HDY1616, xdown, ydown))
                      break;
                  }

                xcur  += hdx;
                ycur  += hdy;
                xdown += HDX1616;
                ydown += HDY1616;
              }

            PDATA += (((offset + 2) << 2));
          }
      }
      break;
    }

  if(FIXMODE & FIX_BIT_GRAPHICS_STEP_Y)
    {
      YPOS1616 = ycur;
      YPOS     = (YPOS1616 / 65536.0);
    }
  else
    {
      XPOS1616 = xcur;
      XPOS     = (XPOS1616 / 65536.0);
    }
}

static
void
DrawLRCel_New(void)
{
  int32_t i;
  int32_t j;
  int32_t xcur;
  int32_t ycur;
  int32_t xvert;
  int32_t yvert;
  int32_t xdown;
  int32_t ydown;
  int32_t hdx;
  int32_t hdy;
  uint16_t CURPIX;
  uint16_t LAMV;

  bpp       = BPP[PRE0 & PRE0_BPP_MASK];
  offsetl   = ((bpp < 8) ? 1 : 2);
  pixcount  = 0;
  offset    = ((offsetl == 1) ?
               ((PRE1 & PRE1_WOFFSET8_MASK)  >> PRE1_WOFFSET8_SHIFT) :
               ((PRE1 & PRE1_WOFFSET10_MASK) >> PRE1_WOFFSET10_SHIFT));
  offset   += 2;

  SPRWI = (1 + (PRE1 & PRE1_TLHPCNT_MASK));
  SPRHI = ((((PRE0 & PRE0_VCNT_MASK) >> PRE0_VCNT_SHIFT) << 1) + 2); /* doom fix */

  if(TestInitVisual(0))
    return;

  xvert = XPOS1616;
  yvert = YPOS1616;

  switch(TEXEL_FUN_NUMBER)
    {
    case 0:
      xvert += (TEXTURE_HI_START * VDX1616);
      yvert += (TEXTURE_HI_START * VDY1616);
      /*
        if(SPRHI > TEXTURE_HI_LIM)
        SPRHI = TEXTURE_HI_LIM;
      */

      if(SPRWI > TEXTURE_WI_LIM)
        SPRWI = TEXTURE_WI_LIM;

      for(i = TEXTURE_HI_START; i < TEXTURE_HI_LIM; i++)
        {
          xcur   = (xvert + TEXTURE_WI_START * HDX1616);
          ycur   = (yvert + TEXTURE_WI_START * HDY1616);
          xvert += VDX1616;
          yvert += VDY1616;

          for(j = TEXTURE_WI_START; j < SPRWI; j++)
            {
              CURPIX = PDEC(mreadh((PDATA + XY2OFF(j,i,offset << 2))),&LAMV);

              if(!pproj.Transparent)
                {
                  uint32_t pixel;
                  uint32_t framePixel;

                  if(FIXMODE & FIX_BIT_TIMING_6)
                    framePixel = mreadh((PIXSOURCE+XY2OFF(xcur >> 16,(ycur>>16)<<1,MADAM.RMOD)));
                  else
                    framePixel = mreadh((PIXSOURCE+XY2OFF(xcur >> 16,ycur>>16,MADAM.RMOD)));

                  pixel = PPROC(CURPIX,framePixel,LAMV);
                  pixel = PPROJ_OUTPUT(CURPIX,pixel,framePixel);
                  mwriteh((FBTARGET+XY2OFF(xcur >> 16,ycur >> 16,MADAM.WMOD)),pixel);
                }

              xcur += HDX1616;
              ycur += HDY1616;
            }
        }
      break;
    case 1:
      {
        int32_t drawHeight;

        drawHeight = VDY1616;
        if((SCBFLAGS & SCB_MARIA) && (drawHeight > (1 << 16)))
          drawHeight = (1 << 16);

        for(i = 0; i < SPRHI; i++)
          {
            xcur   = xvert;
            ycur   = yvert;
            xvert += VDX1616;
            yvert += VDY1616;

            for(j = 0; j < SPRWI; j++)
              {
                CURPIX = PDEC(mreadh((PDATA+XY2OFF(j,i,offset<<2))),&LAMV);

                if(!pproj.Transparent)
                  {
                    if(TexelDraw_Scale(CURPIX,
                                       LAMV,
                                       xcur >> 16,
                                       ycur >> 16,
                                       ((xcur+HDX1616+VDX1616)>>16),
                                       ((ycur+HDY1616+drawHeight)>>16)))
                      break;
                  }

                xcur += HDX1616;
                ycur += HDY1616;
              }
          }
      }
      break;
    default:
      for(i = 0; i < SPRHI; i++)
        {
          xcur     = xvert;
          ycur     = yvert;
          xvert   += VDX1616;
          yvert   += VDY1616;
          xdown    = xvert;
          ydown    = yvert;
          hdx      = HDX1616;
          hdy      = HDY1616;
          HDX1616 += HDDX1616;
          HDY1616 += HDDY1616;

          for(j = 0; j < SPRWI; j++)
            {
              CURPIX = PDEC(mreadh((PDATA+XY2OFF(j,i,offset<<2))),&LAMV);

              if(!pproj.Transparent)
                {
                  if(TexelDraw_Arbitrary(CURPIX,
                                         LAMV,
                                         xcur,
                                         ycur,
                                         xcur + hdx,
                                         ycur + hdy,
                                         xdown + HDX1616,
                                         ydown + HDY1616,
                                         xdown,
                                         ydown))
                    break;
                }

              xcur  += hdx;
              ycur  += hdy;
              xdown += HDX1616;
              ydown += HDY1616;
            }
        }
      break;
    }

  if(FIXMODE & FIX_BIT_GRAPHICS_STEP_Y)
    {
      YPOS1616 = ycur;
      YPOS     = YPOS1616 / 65536.0;
    }
  else
    {
      XPOS1616 = xcur;
      XPOS     = XPOS1616 / 65536.0;
    }
}

/*
  1 word     = 2 CELCYCLES
  1 hword    = 1 CELCYCLE
  8 CELCYCLE = 1 CPU cycles
*/

uint32_t
freedo_madam_cel_get_cycles(void)
{
  const uint32_t val = CELCYCLES;

  CELCYCLES = 0;

  return val;
}

void
freedo_madam_reset(void)
{
  uint32_t i;

  for(i = 0; i < MADAM_REGISTER_COUNT; i++)
    MADAM.mregs[i] = 0;
}

static
INLINE
uint32_t
TexelCCWTest(const double hdx_,
             const double hdy_,
             const double vdx_,
             const double vdy_)
{
  if(((hdx_ + vdx_) * (hdy_ - vdy_) + (vdx_ * vdy_) - (hdx_ * hdy_)) < 0.0)
    return SCB_ACCW;
  return SCB_ACW;
}

static
bool
QuardCCWTest(int32_t wdt_)
{
  float wdt;
  uint32_t tmp;

  if((SCBFLAGS & SCB_ACCW) && (SCBFLAGS & SCB_ACW))
    return false;

  wdt = (float)wdt_;
  tmp = TexelCCWTest(HDX,HDY,VDX,VDY);
  if(tmp != TexelCCWTest(HDX,HDY,VDX+HDDX*wdt,VDY+HDDY*wdt))
    return false;
  if(tmp != TexelCCWTest(HDX+HDDX*SPRHI,HDY+HDDY*SPRHI,VDX,VDY))
    return false;
  if(tmp != TexelCCWTest(HDX+HDDX*SPRHI,HDY+HDDY*SPRHI,VDX+HDDX*(float)SPRHI*wdt,VDY+HDDY*(float)SPRHI*wdt))
    return false;
  if(tmp == (SCBFLAGS & (SCB_ACCW | SCB_ACW)))
    return true;
  return false;
}

static
FORCEINLINE
int32_t
ABS(const int32_t val_)
{
  return ((val_ > 0) ? val_ : -val_);
}

static
int32_t
TestInitVisual(int32_t packed_)
{
  int32_t xpoints[4];
  int32_t ypoints[4];

  if(!(SCBFLAGS & SCB_ACCW) && !(SCBFLAGS & SCB_ACW))
    return -1;

  if(!packed_)
    {
      xpoints[0] = (XPOS1616 >> 16);
      xpoints[1] = (XPOS1616+HDX1616*SPRWI)>>16;
      xpoints[2] = (XPOS1616+VDX1616*SPRHI)>>16;
      xpoints[3] = ((XPOS1616+VDX1616*SPRHI+
                     (HDX1616+HDDX1616*SPRHI)*SPRWI) >> 16);
      if((xpoints[0] < 0) &&
         (xpoints[1] < 0) &&
         (xpoints[2] < 0) &&
         (xpoints[3] < 0))
        return -1;
      if((xpoints[0] > CLIPXVAL) &&
         (xpoints[1] > CLIPXVAL) &&
         (xpoints[2] > CLIPXVAL) &&
         (xpoints[3] > CLIPXVAL))
        return -1;

      ypoints[0] = (YPOS1616 >> 16);
      ypoints[1] = ((YPOS1616+HDY1616*SPRWI) >> 16);
      ypoints[2] = ((YPOS1616+VDY1616*SPRHI) >> 16);
      ypoints[3] = ((YPOS1616+VDY1616*SPRHI+
                     (HDY1616+HDDY1616*SPRHI)*SPRWI) >> 16);
      if((ypoints[0] < 0) &&
         (ypoints[1] < 0) &&
         (ypoints[2] < 0) &&
         (ypoints[3] < 0))
        return -1;
      if((ypoints[0] > CLIPYVAL) &&
         (ypoints[1] > CLIPYVAL) &&
         (ypoints[2] > CLIPYVAL) &&
         (ypoints[3] > CLIPYVAL))
        return -1;
    }
  else
    {
      xpoints[0] = (XPOS1616 >> 16);
      xpoints[1] = ((XPOS1616 + VDX1616 * SPRHI) >> 16);
      if((xpoints[0] < 0) &&
         (xpoints[1] < 0) &&
         (HDX1616   <= 0) &&
         (HDDX1616  <= 0))
        return -1;
      if((xpoints[0] > CLIPXVAL) &&
         (xpoints[1] > CLIPXVAL) &&
         (HDX1616   >= 0)        &&
         (HDDX1616  >= 0))
        return -1;

      ypoints[0] = (YPOS1616 >> 16);
      ypoints[1] = ((YPOS1616 + VDY1616 * SPRHI) >> 16);
      if((ypoints[0] < 0) &&
         (ypoints[1] < 0) &&
         (HDY1616   <= 0) &&
         (HDDY1616  <= 0))
        return -1;
      if((ypoints[0] > CLIPYVAL) &&
         (ypoints[1] > CLIPYVAL) &&
         (HDY1616   >= 0) &&
         (HDDY1616  >= 0))
        return -1;
    }

  if((HDDX1616 == 0) && (HDDY1616 == 0))
    {
      if((HDX1616 == 0) && (VDY1616 == 0))
        {
          if(((HDY1616 < 0) && (VDX1616 > 0)) ||
             ((HDY1616 > 0) && (VDX1616 < 0)))
            {
              if(SCBFLAGS & SCB_ACW)
                {
                  if((ABS(HDY1616) == 0x10000) &&
                     (ABS(VDX1616) == 0x10000) &&
                     !((YPOS1616|XPOS1616)&0xffff))
                    {
                      return Init_Line_Map();
                    }
                  else
                    {
                      Init_Scale_Map();
                      return 0;
                    }
                }
            }
          else
            {
              if(SCBFLAGS & SCB_ACCW)
                {
                  if((ABS(HDY1616) == 0x10000) &&
                     (ABS(VDX1616) == 0x10000) &&
                     !((YPOS1616|XPOS1616)&0xffff))
                    {
                      return Init_Line_Map();
                    }
                  else
                    {
                      Init_Scale_Map();
                      return 0;
                    }
                }
            }

          return -1;
        }
      else if((HDY1616 == 0) && (VDX1616 == 0))
        {
          if(((HDX1616 < 0) && (VDY1616 > 0)) ||
             ((HDX1616 > 0) && (VDY1616 < 0)))
            {
              if(SCBFLAGS & SCB_ACCW)
                {
                  if((ABS(HDX1616) == 0x10000) &&
                     (ABS(VDY1616) == 0x10000) &&
                     !((YPOS1616|XPOS1616)&0xffff))
                    {
                      return Init_Line_Map();
                    }
                  else
                    {
                      Init_Scale_Map();
                      return 0;
                    }
                }
            }
          else
            {
              if(SCBFLAGS & SCB_ACW)
                {
                  if((ABS(HDX1616) == 0x10000) &&
                     (ABS(VDY1616) == 0x10000) &&
                     !((YPOS1616|XPOS1616)&0xffff))
                    {
                      return Init_Line_Map();
                    }
                  else
                    {
                      Init_Scale_Map();
                      return 0;
                    }
                }
            }

          return -1;
        }
    }

  if(QuardCCWTest(!packed_ ? SPRWI : 2048))
    return -1;

  Init_Arbitrary_Map();

  return 0;
}

static
int
Init_Line_Map(void)
{
  TEXEL_FUN_NUMBER = 0;
  TEXTURE_WI_START = 0;
  TEXTURE_HI_START = 0;
  TEXTURE_HI_LIM   = SPRHI;

  if(HDX1616 < 0)
    XPOS1616 -= 0x8000;
  else if(VDX1616 < 0)
    XPOS1616 -= 0x8000;

  if(HDY1616 < 0)
    YPOS1616 -= 0x8000;
  else if(VDY1616 < 0)
    YPOS1616 -= 0x8000;

  if(VDX1616 < 0)
    {
      if(((XPOS1616 - ((SPRHI - 1) << 16)) >> 16) < 0)
        TEXTURE_HI_LIM = ((XPOS1616 >> 16) + 1);
      if(TEXTURE_HI_LIM > SPRHI)
        TEXTURE_HI_LIM = SPRHI;
    }
  else if(VDX1616 > 0)
    {
      if(((XPOS1616 + (SPRHI << 16)) >> 16) > CLIPXVAL)
        TEXTURE_HI_LIM = (CLIPXVAL - (XPOS1616>>16) + 1);
    }

  if(VDY1616 < 0)
    {
      if((((YPOS1616) - ((SPRHI - 1) << 16)) >> 16) < 0)
        TEXTURE_HI_LIM = ((YPOS1616 >> 16) + 1);
      if(TEXTURE_HI_LIM > SPRHI)
        TEXTURE_HI_LIM = SPRHI;
    }
  else if(VDY1616 > 0)
    {
      if(((YPOS1616 + (SPRHI << 16)) >> 16) > CLIPYVAL)
        TEXTURE_HI_LIM = (CLIPYVAL - (YPOS1616 >> 16) + 1);
    }

  if(HDX1616 < 0)
    TEXTURE_WI_LIM = ((XPOS1616 >> 16) + 1);
  else if(HDX1616 > 0)
    TEXTURE_WI_LIM = (CLIPXVAL - (XPOS1616 >> 16) + 1);

  if(HDY1616 < 0)
    TEXTURE_WI_LIM = ((YPOS1616 >> 16) + 1);
  else if(HDY1616 > 0)
    TEXTURE_WI_LIM = (CLIPYVAL - (YPOS1616 >> 16) + 1);


  if(XPOS1616 < 0)
    {
      if(HDX1616 < 0)
        return -1;
      else if(HDX1616 > 0)
        TEXTURE_WI_START = -(XPOS1616 >> 16);

      if(VDX1616 < 0)
        return -1;
      else if(VDX1616 > 0)
        TEXTURE_HI_START = -(XPOS1616 >> 16);
    }
  else if((XPOS1616 >> 16) > CLIPXVAL)
    {
      if(HDX1616 > 0)
        return -1;
      else if(HDX1616 < 0)
        TEXTURE_WI_START = ((XPOS1616 >> 16) - CLIPXVAL);

      if(VDX1616 > 0)
        return -1;
      else if(VDX1616 < 0)
        TEXTURE_HI_START = ((XPOS1616 >> 16) - CLIPXVAL);
    }

  if(YPOS1616 < 0)
    {
      if(HDY1616 < 0)
        return -1;
      else if(HDY1616 > 0)
        TEXTURE_WI_START = -(YPOS1616 >> 16);

      if(VDY1616 < 0)
        return -1;
      else if(VDY1616 > 0)
        TEXTURE_HI_START = -(YPOS1616 >> 16);
    }
  else if((YPOS1616 >> 16) > CLIPYVAL)
    {
      if(HDY1616 > 0)
        return -1;
      else if(HDY1616 < 0)
        TEXTURE_WI_START = ((YPOS1616 >> 16) - CLIPYVAL);

      if(VDY1616 > 0)
        return -1;
      else if(VDY1616 < 0)
        TEXTURE_HI_START = ((YPOS1616 >> 16) - CLIPYVAL);
    }

  /*
    if(TEXTURE_WI_START<((PRE0>>24)&0xf))
    TEXTURE_WI_START=((PRE0>>24)&0xf);
    TEXTURE_WI_START+=(PRE0>>24)&0xf;
    if(TEXTURE_WI_START<0)TEXTURE_WI_START=0;
    if(TEXTURE_HI_START<0)TEXTURE_HI_START=0;
    if(TEXTURE_HI_LIM>SPRHI)TEXTURE_HI_LIM=SPRHI;
  */

  if(TEXTURE_WI_LIM <= 0)
    return -1;

  return 0;
}

static
INLINE
void
Init_Scale_Map(void)
{
  int32_t deltax;
  int32_t deltay;

  TEXEL_FUN_NUMBER = 1;
  if(HDX1616 < 0)
    XPOS1616 -= 0x8000;
  else if(VDX1616 < 0)
    XPOS1616 -= 0x8000;

  if(HDY1616 < 0)
    YPOS1616 -= 0x8000;
  else if(VDY1616 < 0)
    YPOS1616 -= 0x8000;

  deltax = (HDX1616 + VDX1616);
  deltay = (HDY1616 + VDY1616);
  if(deltax < 0)
    TEXEL_INCX = -1;
  else
    TEXEL_INCX = 1;

  if(deltay < 0)
    TEXEL_INCY = -1;
  else
    TEXEL_INCY = 1;

  TEXTURE_WI_START = 0;
  TEXTURE_HI_START = 0;
}

static
INLINE
void
Init_Arbitrary_Map(void)
{
  TEXEL_FUN_NUMBER = 2;
  TEXTURE_WI_START = 0;
  TEXTURE_HI_START = 0;
}

static
void
TexelDraw_Line(uint16_t CURPIX_,
               uint16_t LAMV_,
               int32_t  xcur_,
               int32_t  ycur_,
               int32_t  cnt_)
{
  int32_t i;
  uint32_t curr;
  uint32_t pixel;

  xcur_ >>= 16;
  ycur_ >>= 16;
  curr = 0xFFFFFFFF;

  for(i = 0; i < cnt_; i++, xcur_ += (HDX1616 >> 16), ycur_ += (HDY1616 >> 16))
    {
      uint32_t next;

      next = mreadh((PIXSOURCE + XY2OFF(xcur_,ycur_,MADAM.RMOD)));
      if(next != curr)
        {
          curr  = next;
          pixel = PPROC(CURPIX_,next,LAMV_);
        }

      //pixel=PPROC(CURPIX,mreadh((PIXSOURCE+XY2OFF(xcur>>16,ycur>>16,MADMA.RMOD))),LAMV);
      int foo = xcur_;
      if(PDATA == 0x139630)
        {
          foo -= 12;
        }
      pixel = PPROJ_OUTPUT(CURPIX_,pixel,next);
      mwriteh((FBTARGET + XY2OFF(foo,ycur_,MADAM.WMOD)),pixel);
    }
}

static
INLINE
uint16_t
readPIX(uint32_t src_,
        int32_t  i_,
        int32_t  j_)
{
  src_ += XY2OFF(j_ >> HIRESMODE,i_ >> HIRESMODE,MADAM.WMOD);
  if(HIRESMODE)
    return *((uint16_t*)&Mem[(src_ ^ 2) + (((i_ & 1) << 1) + (j_ & 1)) * 1024 * 1024]);
  return *((uint16_t*)&Mem[src_ ^ 2]);
}

static
INLINE
void
writePIX(uint32_t src_,
         int32_t  i_,
         int32_t  j_,
         uint16_t pix_)
{
  src_ += XY2OFF(j_ >> HIRESMODE,i_ >> HIRESMODE,MADAM.WMOD);
  if(HIRESMODE)
    *((uint16_t*)&Mem[(src_ ^ 2) + (((i_ & 1) << 1) + (j_ & 1)) * 1024 * 1024]) = pix_;
  else
    *((uint16_t*)&Mem[src_ ^ 2]) = pix_;
}

static
int
TexelDraw_Scale(uint16_t CURPIX_,
                uint16_t LAMV_,
                int32_t  xcur_,
                int32_t  ycur_,
                int32_t  deltax_,
                int32_t  deltay_)
{
  int32_t x;
  int32_t y;
  uint32_t pixel;
  uint32_t framePixel;

  if(FIXMODE & FIX_BIT_TIMING_3)
    {
      deltay_ *= 5;
      ycur_   *= 5;
    }

  if((HDX1616 < 0) && (deltax_ < 0) && (xcur_ < 0))
    return -1;
  else if((HDY1616 < 0) && (deltay_ < 0) && (ycur_ < 0))
    return -1;
  else if((HDX1616 > 0) && (deltax_ > CLIPXVAL) && (xcur_ > CLIPXVAL))
    return -1;
  else if((HDY1616 > 0) && (deltay_ > CLIPYVAL) && (ycur_ > CLIPYVAL))
    return -1;

  if(xcur_ == deltax_)
    return 0;

  for(y = ycur_; y != deltay_; y += TEXEL_INCY)
    {
      for(x = xcur_; x != deltax_; x += TEXEL_INCX)
        {
          if(TESTCLIP(x,y))
            {
              framePixel = mreadh(PIXSOURCE + XY2OFF(x,y,MADAM.RMOD));
              pixel      = PPROC(CURPIX_,framePixel,LAMV_);
              pixel      = PPROJ_OUTPUT(CURPIX_,pixel,framePixel);
              /*
                next = mreadh((PIXSOURCE+XY2OFF(j,i,MADMA.RMOD)));
                if(next != curr)
                  {
                    curr = next;
                    pixel = PPROC(CURPIX,next,LAMV);
                  }
              */
              mwriteh((FBTARGET + XY2OFF(x,y,MADAM.WMOD)),pixel);
            }
        }
    }

  return 0;
}

static
int32_t
TexelDraw_Arbitrary(uint16_t CURPIX_,
                    uint16_t LAMV_,
                    int32_t  xA_,
                    int32_t  yA_,
                    int32_t  xB_,
                    int32_t  yB_,
                    int32_t  xC_,
                    int32_t  yC_,
                    int32_t  xD_,
                    int32_t  yD_)
{
  int32_t i;
  int32_t j;
  int32_t miny;
  int32_t maxy;
  int32_t maxx;
  int32_t maxxt;
  int32_t maxyt;
  int32_t jtmp;
  uint32_t curr;
  uint32_t next;
  uint32_t pixel;
  int32_t xpoints[4];
  int32_t updowns[4];

  curr = -1;
  xA_ >>= (16 - HIRESMODE);
  xB_ >>= (16 - HIRESMODE);
  xC_ >>= (16 - HIRESMODE);
  xD_ >>= (16 - HIRESMODE);
  yA_ >>= (16 - HIRESMODE);
  yB_ >>= (16 - HIRESMODE);
  yC_ >>= (16 - HIRESMODE);
  yD_ >>= (16 - HIRESMODE);

  if((xA_ == xB_) && (xB_ == xC_) && (xC_ == xD_))
    return 0;

  maxxt = ((CLIPXVAL + 1) << HIRESMODE);
  maxyt = ((CLIPYVAL + 1) << HIRESMODE);

  if((HDX1616 < 0) && (HDDX1616 < 0))
    {
      if((xA_ < 0) && (xB_ < 0) && (xC_ < 0) && (xD_ < 0))
        return -1;
    }

  if((HDX1616 > 0) && (HDDX1616 > 0))
    {
      if((xA_ >= maxxt) && (xB_ >= maxxt) && (xC_ >= maxxt) && (xD_ >= maxxt))
        return -1;
    }

  if((HDY1616 < 0) && (HDDY1616 < 0))
    {
      if((yA_ < 0) && (yB_ < 0) && (yC_ < 0) && (yD_ < 0))
        return -1;
    }

  if((HDY1616 > 0) && (HDDY1616 > 0))
    {
      if((yA_ >= maxyt) && (yB_ >= maxyt) && (yC_ >= maxyt) && (yD_ >= maxyt))
        return -1;
    }

  miny = maxy = yA_;

  if(miny > yB_)
    miny = yB_;
  if(miny > yC_)
    miny = yC_;
  if(miny > yD_)
    miny = yD_;
  if(maxy < yB_)
    maxy = yB_;
  if(maxy < yC_)
    maxy = yC_;
  if(maxy < yD_)
    maxy = yD_;

  i = miny;
  if(i < 0)
    i = 0;
  if(maxy < maxyt)
    maxyt = maxy;

  for(; i < maxyt; i++)
    {
      int cnt_cross = 0;
      if((i < yB_) && (i >= yA_))
        {
          xpoints[cnt_cross] = (int32_t)((((xB_-xA_)*(i-yA_))/(yB_-yA_))+xA_);
          updowns[cnt_cross++] = 1;
        }
      else if((i >= yB_) && (i < yA_))
        {
          xpoints[cnt_cross] = (int32_t)((((xA_-xB_)*(i-yB_))/(yA_-yB_))+xB_);
          updowns[cnt_cross++] = 0;
        }

      if((i < yC_) && (i >= yB_))
        {
          xpoints[cnt_cross] = (int32_t)((((xC_-xB_)*(i-yB_))/(yC_-yB_))+xB_);
          updowns[cnt_cross++] = 1;
        }
      else if((i >= yC_) && (i < yB_))
        {
          xpoints[cnt_cross] = (int32_t)((((xB_-xC_)*(i-yC_))/(yB_-yC_))+xC_);
          updowns[cnt_cross++] = 0;
        }

      if((i < yD_) && (i >= yC_))
        {
          xpoints[cnt_cross] = (int32_t)((((xD_-xC_)*(i-yC_))/(yD_-yC_))+xC_);
          updowns[cnt_cross++] = 1;
        }
      else if((i >= yD_) && (i < yC_))
        {
          xpoints[cnt_cross] = (int32_t)((((xC_-xD_)*(i-yD_))/(yC_-yD_))+xD_);
          updowns[cnt_cross++] = 0;
        }

      if(cnt_cross & 1)
        {
          if((i < yA_) && (i >= yD_))
            {
              xpoints[cnt_cross] = (int32_t)((((xA_-xD_)*(i-yD_))/(yA_-yD_))+xD_);
              updowns[cnt_cross] = 1;
            }
          else if((i >= yA_) && (i < yD_))
            {
              xpoints[cnt_cross] = (int32_t)((((xD_-xA_)*(i-yA_))/(yD_-yA_))+xA_);
              updowns[cnt_cross] = 0;
            }
        }

      if(cnt_cross != 0)
        {
          if(xpoints[0] > xpoints[1])
            {
              xpoints[1] += xpoints[0];
              xpoints[0]  = xpoints[1] - xpoints[0];
              xpoints[1]  = xpoints[1] - xpoints[0];

              jtmp       = updowns[0];
              updowns[0] = updowns[1];
              updowns[1] = jtmp;
            }

          if(cnt_cross > 2)
            {
              if(((SCBFLAGS & SCB_ACW)  && (updowns[2] == 0)) ||
                 ((SCBFLAGS & SCB_ACCW) && (updowns[2] == 1)))
                {
                  j = xpoints[2];
                  if(j < 0)
                    j = 0;

                  maxx = xpoints[3];
                  if(maxx > maxxt)
                    maxx = maxxt;

                  for(; j < maxx; j++)
                    {
                      next = readPIX(PIXSOURCE,i,j);
                      if(next != curr)
                        {
                          curr  = next;
                          pixel = PPROC(CURPIX_,next,LAMV_);
                          pixel = PPROJ_OUTPUT(CURPIX_,pixel,next);
                        }
                      writePIX(FBTARGET,i,j,pixel);
                    }
                }
            }

          if(((SCBFLAGS & SCB_ACW)  && (updowns[0] == 0)) ||
             ((SCBFLAGS & SCB_ACCW) && (updowns[0] == 1)))
            {
              j = xpoints[0];
              if(j < 0)
                j = 0;

              maxx = xpoints[1];
              if(maxx > maxxt)
                maxx = maxxt;

              for(; j < maxx; j++)
                {
                  next = readPIX(PIXSOURCE,i,j);
                  if(next != curr)
                    {
                      curr  = next;
                      pixel = PPROC(CURPIX_,next,LAMV_);
                      pixel = PPROJ_OUTPUT(CURPIX_,pixel,next);
                    }
                  writePIX(FBTARGET,i,j,pixel);
                }
            }
        }
    }

  return 0;
}
