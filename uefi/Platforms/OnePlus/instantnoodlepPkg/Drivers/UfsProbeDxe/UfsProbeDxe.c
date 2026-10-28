/** @file
  Drive the UFS host far enough to read the device capacity, then stop.

  The layout follows ShengUfs's RootCommands.h so the same sequence can move into the
  kernel driver unchanged:

    buffer + 0x0000  UTRD (32 bytes), OCS in the low byte of word 2
    buffer + 0x1000  command UPIU
    buffer + 0x1200  response UPIU
    buffer + 0x2000  data area

  Words 4/5 of the UTRD carry the command UPIU address, word 6 the response length and
  offset in 4-byte units, word 7 the PRDT count and offset in 4-byte units.
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>

#define UFS_HOST_BASE   0x01D84000ULL
#define UFS_PHY_BASE    0x01D87000ULL

#define UFS_IS          0x0020
#define UFS_IE          0x0024
#define UFS_HCS         0x0030
#define UFS_HCE         0x0034
#define UFS_UTRLBA      0x0050
#define UFS_UTRLBAU     0x0054
#define UFS_UTRLDBR     0x0058
#define UFS_UTRLRSR     0x005C
#define UFS_UICCMD      0x0090
#define UFS_UICCMDARG1  0x0094
#define UFS_UICCMDARG2  0x0098

#define GCC_BASE        0x00100000ULL
#define GCC_UFS_BCR     0x00077000
#define GCC_UFS_AXI     0x00077010
#define GCC_UFS_AHB     0x00077018
#define GCC_UFS_TX0     0x0007701C
#define GCC_UFS_RX0     0x00077020
#define GCC_UFS_UNIPRO  0x0007705C
#define GCC_UFS_ICE     0x00077064
#define GCC_UFS_AUX     0x0007709C
#define GCC_UFS_RX1     0x000770B8
#define GCC_UFS_AGGR    0x000770CC

#define UIC_LINK_STARTUP 0x16

#define URD_OFFSET      0x0000
#define UCD_OFFSET      0x1000
#define RSP_OFFSET      0x0200
#define PRDT_OFFSET     0x1000
#define DATA_OFFSET     0x2000
#define BUFFER_PAGES    8

static UINT8  *mBuffer;
static UINT64  mPhys;
static UINT64  mUtRdBase;

static VOID
SubmitCommand (
  IN  UINT8   Direction,
  IN  UINT32  DataBytes
  )
{
  UINT32  *Words;

  Words = (UINT32 *)(UINTN)mUtRdBase;
  ZeroMem ((VOID *)(UINTN)mUtRdBase, 32);
  ZeroMem (mBuffer + UCD_OFFSET + PRDT_OFFSET, 16);
  Words[0] = 0x10000000U | ((UINT32)Direction << 25);
  Words[2] = 0x0FU;
  Words[4] = (UINT32)(mPhys + UCD_OFFSET);
  Words[5] = (UINT32)((mPhys + UCD_OFFSET) >> 32);
  Words[6] = (512U >> 2) | ((RSP_OFFSET >> 2) << 16);
  Words[7] = (DataBytes ? 1U : 0U) | ((PRDT_OFFSET >> 2) << 16);

  if (DataBytes != 0) {
    UINT32  *Prd;
    Prd    = (UINT32 *)(mBuffer + UCD_OFFSET + PRDT_OFFSET);
    Prd[0] = (UINT32)(mPhys + DATA_OFFSET);
    Prd[1] = (UINT32)((mPhys + DATA_OFFSET) >> 32);
    Prd[2] = 0;
    Prd[3] = 0;
    Prd[4] = 0;
    Prd[5] = 0;
  }

  WriteBackDataCacheRange (mBuffer, BUFFER_PAGES * 4096);
  WriteBackDataCacheRange ((VOID *)(UINTN)mUtRdBase, 32);
  Print (L"UFSPROBE:   utrd %08x %08x %08x %08x %08x %08x %08x %08x\n",
         Words[0], Words[1], Words[2], Words[3], Words[4], Words[5], Words[6], Words[7]);
  Print (L"UFSPROBE:   prd %08x %08x %08x %08x (dir %u len %u)\n",
         *(UINT32 *)(mBuffer + UCD_OFFSET + PRDT_OFFSET),
         *(UINT32 *)(mBuffer + UCD_OFFSET + PRDT_OFFSET + 4),
         *(UINT32 *)(mBuffer + UCD_OFFSET + PRDT_OFFSET + 8),
         *(UINT32 *)(mBuffer + UCD_OFFSET + PRDT_OFFSET + 12),
         Direction, DataBytes);
  MmioWrite32 ((UINTN)UFS_HOST_BASE + UFS_UTRLDBR, 1);
}

static UINT32
PollOcs (
  IN UINTN  Milliseconds
  )
{
  UINTN   Index;
  UINT32  Ocs;

  for (Index = 0; Index < Milliseconds; Index++) {
    InvalidateDataCacheRange ((VOID *)(UINTN)mUtRdBase, 32);
    Ocs = *(volatile UINT32 *)(UINTN)(mUtRdBase + 8) & 0xFF;
    if (Ocs != 0x0F) {
      return Ocs;
    }
    gBS->Stall (1000);
  }
  return 0x0F;
}

EFI_STATUS
EFIAPI
UfsProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status = EFI_SUCCESS;
  UINT32      Hcs;
  UINT32      Ocs;
  UINT8       *Ucd;
  UINT8       *Rsp;
  UINT32      LastLba;
  UINT32      BlockSize;

  Hcs = MmioRead32 ((UINTN)UFS_HOST_BASE + UFS_HCS);
  Print (L"\nUFSPROBE: HCS 0x%08x, taking the queue\n", Hcs);

  {
    EFI_PHYSICAL_ADDRESS  At = 0x9F600000ULL;
    Status = gBS->AllocatePages (AllocateAddress, EfiBootServicesData, BUFFER_PAGES, &At);
    if (EFI_ERROR (Status)) {
      Print (L"UFSPROBE: allocation at 0x9F600000 failed: %r\n", Status);
      CpuDeadLoop ();
    }
    mBuffer = (UINT8 *)(UINTN)At;
  }
  ZeroMem (mBuffer, BUFFER_PAGES * 4096);
  mPhys   = (UINT64)(UINTN)mBuffer;

  mUtRdBase = mPhys;
  MmioWrite32 ((UINTN)UFS_HOST_BASE + UFS_UTRLBA, (UINT32)mPhys);
  MmioWrite32 ((UINTN)UFS_HOST_BASE + UFS_UTRLBAU, (UINT32)(mPhys >> 32));
  MmioWrite32 ((UINTN)UFS_HOST_BASE + UFS_UTRLRSR, 1);
  Print (L"UFSPROBE: UTRLBA 0x%lx RSR 0x%x\n",
         mPhys, MmioRead32 ((UINTN)UFS_HOST_BASE + UFS_UTRLRSR));

  Ucd = mBuffer + UCD_OFFSET;
  Rsp = mBuffer + UCD_OFFSET + RSP_OFFSET;

  Ucd[3] = 1;
  SubmitCommand (0, 0);
  Ocs = PollOcs (2000);
  Print (L"UFSPROBE: NOP ocs 0x%x response-type 0x%x tag %u\n", Ocs, Rsp[0], Rsp[3]);

  ZeroMem (Ucd, 256);
  Ucd[0] = 0x01;
  Ucd[1] = 0x40;
  Ucd[2] = 0;
  Ucd[12] = 0;
  Ucd[13] = 0;
  Ucd[14] = 0;
  Ucd[15] = 8;
  Ucd[16] = 0x25;
  SubmitCommand (2, 8);
  Ocs = PollOcs (2000);
  InvalidateDataCacheRange (mBuffer + DATA_OFFSET, 64);
  Print (L"UFSPROBE:   where 0x%08x 0x%08x (at 0x2000, 0x3000)\n",
         *(UINT32 *)(mBuffer + 0x2000), *(UINT32 *)(mBuffer + 0x3000));
  {
    volatile UINT8  *Data;
    UINT32          Word[4];
    UINTN           Index;

    Data = (volatile UINT8 *)(mBuffer + DATA_OFFSET);
    for (Index = 0; Index < 4; Index++) {
      Word[Index] = ((UINT32)Data[Index * 4 + 0] << 24) | ((UINT32)Data[Index * 4 + 1] << 16) |
                    ((UINT32)Data[Index * 4 + 2] << 8) | (UINT32)Data[Index * 4 + 3];
    }
    LastLba   = Word[0];
    BlockSize = Word[1];
    Print (L"UFSPROBE: READ CAPACITY ocs 0x%x status 0x%x residual %u\n",
           Ocs, Rsp[6], ((UINT32)Rsp[12] << 8) | Rsp[13]);
    Print (L"UFSPROBE: data %08x %08x %08x %08x\n", Word[0], Word[1], Word[2], Word[3]);
  }
  Print (L"UFSPROBE: last-lba %lu block-size %lu bytes\n", LastLba, BlockSize);
  if (BlockSize != 0) {
    Print (L"UFSPROBE: capacity %lu MiB\n", ((UINT64)LastLba + 1) * BlockSize / (1024 * 1024));
  }

  /* READ(10) LBA 1, one block, into the data area: the GPT header lives there. */
  ZeroMem (Ucd, 64);
  Ucd[0]  = 0x01;
  Ucd[1]  = 0x40;
  Ucd[2]  = 0;
  Ucd[12] = 0x00;
  Ucd[13] = 0x00;
  Ucd[14] = 0x10;
  Ucd[15] = 0x00;
  Ucd[16] = 0x28;
  Ucd[21] = 0x01;
  Ucd[22] = 0x00;
  Ucd[23] = 0x00;
  Ucd[24] = 0x01;
  SubmitCommand (2, 4096);
  Ocs = PollOcs (2000);
  InvalidateDataCacheRange (mBuffer + DATA_OFFSET, 4096);
  {
    volatile UINT8  *Data;
    CHAR8           Text[9];
    UINTN           Index;

    Data = (volatile UINT8 *)(mBuffer + DATA_OFFSET);
    for (Index = 0; Index < 8; Index++) {
      Text[Index] = (CHAR8)Data[Index];
    }
    Text[8] = 0;
    Print (L"UFSPROBE: READ(10) lba1 ocs 0x%x status 0x%x sig '%a'\n", Ocs, Rsp[6], Text);
    Print (L"UFSPROBE: lba1 %02x %02x %02x %02x %02x %02x %02x %02x\n",
           Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7]);
  }

  CpuDeadLoop ();
  return Status;
}
