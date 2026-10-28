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
#define UFS_HCS         0x0030
#define UFS_HCE         0x0034
#define UFS_UTRLBA      0x0050
#define UFS_UTRLBAU     0x0054
#define UFS_UTRLDBR     0x0058
#define UFS_UTRLRSR     0x005C

#define URD_OFFSET      0x0000
#define UCD_OFFSET      0x1000
#define RSP_OFFSET      0x1200
#define DATA_OFFSET     0x2000
#define BUFFER_PAGES    8

static UINT8  *mBuffer;
static UINT64  mPhys;

static VOID
SubmitCommand (
  IN  UINT8   Direction,
  IN  UINT32  DataBytes
  )
{
  UINT32  *Words;

  Words = (UINT32 *)(mBuffer + URD_OFFSET);
  ZeroMem (mBuffer + URD_OFFSET, 64);
  Words[0] = 0x10000000U | ((UINT32)Direction << 25);
  Words[2] = 0x0FU;
  Words[4] = (UINT32)(mPhys + UCD_OFFSET);
  Words[5] = (UINT32)((mPhys + UCD_OFFSET) >> 32);
  Words[6] = (512U >> 2) | ((RSP_OFFSET - UCD_OFFSET) >> 2 << 16);
  Words[7] = (DataBytes ? 1U : 0U) | (((DataBytes ? (DATA_OFFSET - UCD_OFFSET) : 0) >> 2) << 16);

  if (DataBytes != 0) {
    UINT32  *Prd;
    Prd    = (UINT32 *)(mBuffer + UCD_OFFSET + (DATA_OFFSET - UCD_OFFSET));
    Prd[0] = (UINT32)(mPhys + DATA_OFFSET);
    Prd[1] = (UINT32)((mPhys + DATA_OFFSET) >> 32);
  }

  WriteBackDataCacheRange (mBuffer, BUFFER_PAGES * 4096);
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
    InvalidateDataCacheRange (mBuffer + URD_OFFSET, 64);
    Ocs = *(volatile UINT32 *)(mBuffer + URD_OFFSET + 8) & 0xFF;
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

  mBuffer = AllocatePages (BUFFER_PAGES);
  if (mBuffer == NULL) {
    Print (L"UFSPROBE: allocation failed\n");
    CpuDeadLoop ();
  }
  ZeroMem (mBuffer, BUFFER_PAGES * 4096);
  mPhys   = (UINT64)(UINTN)mBuffer;

  MmioWrite32 ((UINTN)UFS_HOST_BASE + UFS_UTRLBA, (UINT32)mPhys);
  MmioWrite32 ((UINTN)UFS_HOST_BASE + UFS_UTRLBAU, (UINT32)(mPhys >> 32));
  MmioWrite32 ((UINTN)UFS_HOST_BASE + UFS_UTRLRSR, 1);
  Print (L"UFSPROBE: UTRLBA 0x%lx RSR 0x%x\n",
         mPhys, MmioRead32 ((UINTN)UFS_HOST_BASE + UFS_UTRLRSR));

  Ucd = mBuffer + UCD_OFFSET;
  Rsp = mBuffer + RSP_OFFSET;

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
  Print (L"UFSPROBE: READ CAPACITY ocs 0x%x status 0x%x\n", Ocs, Rsp[6]);
  Print (L"UFSPROBE: data %08x %08x %08x %08x\n",
         *(UINT32 *)(mBuffer + DATA_OFFSET + 0), *(UINT32 *)(mBuffer + DATA_OFFSET + 4),
         *(UINT32 *)(mBuffer + DATA_OFFSET + 8), *(UINT32 *)(mBuffer + DATA_OFFSET + 12));
  LastLba   = ((UINT32)mBuffer[DATA_OFFSET + 0] << 24) | ((UINT32)mBuffer[DATA_OFFSET + 1] << 16) |
              ((UINT32)mBuffer[DATA_OFFSET + 2] << 8) | (UINT32)mBuffer[DATA_OFFSET + 3];
  BlockSize = ((UINT32)mBuffer[DATA_OFFSET + 4] << 24) | ((UINT32)mBuffer[DATA_OFFSET + 5] << 16) |
              ((UINT32)mBuffer[DATA_OFFSET + 6] << 8) | (UINT32)mBuffer[DATA_OFFSET + 7];
  Print (L"UFSPROBE: last-lba %lu block-size %lu bytes\n", LastLba, BlockSize);
  if (BlockSize != 0) {
    Print (L"UFSPROBE: capacity %lu MiB\n", ((UINT64)LastLba + 1) * BlockSize / (1024 * 1024));
  }

  CpuDeadLoop ();
  return Status;
}
