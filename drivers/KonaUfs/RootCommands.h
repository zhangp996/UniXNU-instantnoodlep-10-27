// SPDX-License-Identifier: BSD-2-Clause-Patent
// Shared production command path. Included inside the root transport namespace.
// The host harness replaces only MMIO, DMA synchronization, clock and lock primitives.
// native-ufs-slots=2: consecutive commands use different request slots. The request list has 32
// entries and the page carries one 4 KiB command block per slot, so the CPU writes slot 1 while the
// controller may still own slot 0. Reusing one slot, one UTRD and one DMA page is what every dying
// run shares, and a post-delay only shrinks that window instead of closing it.
static unsigned gSlotAlternate;
static unsigned gSlot;
static unsigned gSlotSequence;

static uint8_t *utrdBase (void)
{
  return gHost.virt + (gSlotAlternate ? gSlot * 32U : 0U);
}

static uint8_t *ucdBase (void)
{
  return gHost.virt + kUcdOffset + (gSlotAlternate ? gSlot * kUcdStride : 0U);
}

static uint64_t ucdPhysBase (void)
{
  return gHost.phys + kUcdOffset + (gSlotAlternate ? gSlot * kUcdStride : 0U);
}

struct HardwareTransferPort {
  uint64_t deadline;
  uint32_t doorbell () { return read32 (gHost.hc, 0x58); }
  uint32_t interrupt () { return read32 (gHost.hc, 0x20); }
  uint32_t completedOcs () {
    cacheInvalidate (utrdBase (), 64);
    return *reinterpret_cast<volatile uint32_t *> (utrdBase () + 8) & 0xff;
  }
  bool expired () { return mach_absolute_time () >= deadline; }
  void delay () { IODelay (2); }
  // The poll loop cannot see the diagnostics global (TransferPoll.h is included where it is not
  // declared), so the port reports the loop's progress: 55 means the loop really spins.
  void progress (uint32_t polls) { if (!(polls & 0x3ffU)) traceStep (15); }
};

static void putBe32 (uint8_t *at, uint32_t value)
{
  at[0] = static_cast<uint8_t> (value >> 24);
  at[1] = static_cast<uint8_t> (value >> 16);
  at[2] = static_cast<uint8_t> (value >> 8);
  at[3] = static_cast<uint8_t> (value);
}

static uint32_t loadBe32 (const uint8_t *at)
{
  return (uint32_t (at[0]) << 24) | (uint32_t (at[1]) << 16) | (uint32_t (at[2]) << 8) | at[3];
}

// Issue the UPIU already placed at gHost.virt + kUcdOffset through request slot 0. `direction` is
// the UTRD data direction (0 none, 1 from host, 2 to host). Direction 1 is reachable only through hostWrite4k, which
// confines it to the persistent store window. Returns the overall command status
// (0 = completed) in *ocs. Timeout/fatal status quarantines the allocation.
// native-ufs-post-delay=<us> waits that long after the doorbell clears and before the CPU touches
// the buffer. Zero means no wait, which is what every run so far did.
static uint32_t gPostDelay;

// One transport reset per command. porting/drivers.md asks the transport boundary to define
// "timeout, retry and disconnect handling", and this driver had none: a command that did not
// complete marked the controller poisoned forever. UTRLRSR (0x60) stops and restarts the request
// list, which is the interface's own recovery, and UTRLRDY (HCS bit 2) reports both edges.
static bool gRetried;

// native-ufs-reset-every=<n>: stop and restart the request list every n commands. The block at the
// address a 200 ms run stopped on reads fine on its own, so what accumulates over about 260 commands
// is controller or request-list state, and a periodic UTRLRSR cycle is the cheapest way to tell.
static uint32_t gResetEvery;
static uint32_t gTagRotate;
// native-ufs-noinval=1 skips the cache invalidate that runs once a command has completed. Every
// deterministic stop so far sits at stage 13, which is that call: the poll has returned, the
// controller is healthy, and the CPU touches the DMA buffer next.
static unsigned gNoInvalidate;
static unsigned gChunkBlocks;
// native-ufs-ssu-every: send START STOP UNIT to the device well-known LU every n media reads. The
// stop survived a request-list reset every 32 commands, so if a counter exists it is inside the
// device or the kernel rather than in the host controller, and this poke is the device-side test.
static unsigned gSsuEvery;
// native-ufs-read-blocks: clamp every media request to n 512-byte blocks. The block layer resubmits
// the rest as a new request, so the same bytes arrive as more requests - which separates a stop that
// counts requests from one that counts bytes, the two readings that still fit the evidence.
static unsigned gReadBlocks;
// native-ufs-nocopy: complete each request without copying into the caller's memory descriptor. The
// only thing the driver still touches per request is that copy, and the stop survives every reset,
// poke and size change, so this is the one remaining kernel-side step to isolate.
static unsigned gNoCopy;
// native-ufs-directcpy: copy the caller's slice straight out of the DMA page instead of running the
// data through the bounce buffer first. The command pattern is unchanged (two 4 KiB blocks per
// READ(10), same LBAs), but the CPU moves half as many bytes per request, which is the one variable
// every run so far held fixed while the stop stayed at 259-262 reads. A true zero-copy cannot exist
// on this window: its base 0x104400 is 1024 bytes into a device block, so every 4 KiB DMA block
// straddles the caller's buffer and the shift has to be applied by the CPU somewhere.
static unsigned gDirectCopy;

static bool hostResetTransport (void)
{
  write32 (gHost.hc, 0x60, 0);
  __asm__ volatile ("dsb sy" ::: "memory");
  for (unsigned i = 0; i < 2000; i++) {
    if (!(read32 (gHost.hc, 0x30) & 0x4U)) {
      break;
    }
    IODelay (10);
  }
  write32 (gHost.hc, 0x20, 0xffffffffU);
  write32 (gHost.hc, 0x58, 0);
  write32 (gHost.hc, 0x60, 1);
  __asm__ volatile ("dsb sy" ::: "memory");
  for (unsigned i = 0; i < 2000; i++) {
    if (read32 (gHost.hc, 0x30) & 0x4U) {
      return true;
    }
    IODelay (10);
  }
  return false;
}

static IOReturn hostSubmit (uint32_t direction, uint32_t dataBytes, uint32_t timeoutUs, uint32_t *ocs)
{
  *ocs = 15;
  traceStep (5);
  if (!timeoutUs || dataBytes > kDataMax || (dataBytes & 3) ||
      (dataBytes ? (direction != 1 && direction != 2) : direction != 0)) return kIOReturnBadArgument;
  traceStep (6);
  if (gHost.poisoned) return kIOReturnNotReady;
  if (read32 (gHost.hc, 0x58) != 0) {
    gHost.poisoned = true;
    gHost.ready = false;
    return recordTransport (kIOReturnBusy, *ocs);
  }
  if (read32 (gHost.hc, 0x20) & kUfsFatalStatus) {
    gHost.poisoned = true;
    gHost.ready = false;
    return recordTransport (kIOReturnIOError, *ocs);
  }
  gRootDiagnostic.command++;
  uint8_t *ucd = ucdBase ();
  if (gTagRotate) {
    // The command tag is byte 3 of the UPIU header and every command so far used zero.
    ucd[3] = static_cast<uint8_t> (gRootDiagnostic.command & 0x1fU);
  }
  gRootDiagnostic.type = ucd[0];
  gRootDiagnostic.lun = ucd[2];
  gRootDiagnostic.opcode = ucd[gRootDiagnostic.type == 1 ? 16 : 12];
  gRootDiagnostic.scsi = 0xffff;
  gRootDiagnostic.response = 0;
  gRootDiagnostic.protocol = 0;
  gRootDiagnostic.residual = 0;
  gRootDiagnostic.sense = 0;
  uint8_t *utrd = utrdBase ();
  uint64_t ucdPhys = ucdPhysBase ();
  uint32_t *words = reinterpret_cast<uint32_t *> (utrd);
  for (unsigned index = 0; index < 8; index++) {
    words[index] = 0;
  }
  // DW0: UTRL 1, data direction, and the interrupt bit the working bdevsw driver also sets.
  words[0] = 0x10000000U | (direction << 25) | (gUfsNoIrq ? 0U : 0x01000000U);
  words[2] = 0x0fU;
  words[4] = static_cast<uint32_t> (ucdPhys);
  words[5] = static_cast<uint32_t> (ucdPhys >> 32);
  words[6] = (512U >> 2) | ((kRspOffset >> 2) << 16);
  // A non-data command must not advertise a PRDT entry: the working driver sends none, and an
  // entry left at zero would point the controller at physical address 0.
  words[7] = (dataBytes ? 1U : 0U) | (dataBytes ? ((kPrdtOffset >> 2) << 16) : 0U);
  if (dataBytes) {
    uint64_t dataPhys = gHost.phys + kDataOffset;
    uint32_t *prd = reinterpret_cast<uint32_t *> (ucd + kPrdtOffset);
    prd[0] = static_cast<uint32_t> (dataPhys);
    prd[1] = static_cast<uint32_t> (dataPhys >> 32);
    prd[2] = 0;
    prd[3] = dataBytes - 1;
  }
  traceStep (7);   // UTRD and PRDT built
  traceStep (8);
  cacheCivac (gHost.virt, kDataOffset + dataBytes);
  hostWrite (0x20, 1);  // Acknowledge completion only; preserve error evidence.
  traceStep (9);
  HardwareTransferPort port;
  clock_interval_to_deadline (timeoutUs, kMicrosecondScale, &port.deadline);
  traceStep (10);   // about to write the doorbell
  hostWrite (0x58, 1U << gSlot);
  traceStep (11);   // doorbell written
  UfsTransferPoll state;
  UfsTransferEnd end = pollUfsTransfer (port, state);
  traceStep (12);   // the poll returned (Completed, Fatal or Timeout)
  gRootDiagnostic.polls = state.polls;
  gRootDiagnostic.dbSeen = state.doorbellSeen;
  gRootDiagnostic.isSeen = state.interruptSeen;
  *ocs = state.ocs;
  if (end != UfsTransferEnd::Completed) {
    // A timeout gets one transport reset and one resubmission: the interface's own stop/start
    // cycle, which is what porting/drivers.md means by retry handling. A fatal status or a second
    // failure still quarantines the allocation, because the controller may own the buffer.
    if (end == UfsTransferEnd::Timeout && !gRetried && hostResetTransport ()) {
      gRetried = true;
      return hostSubmit (direction, dataBytes, timeoutUs, ocs);
    }
    gRetried = false;
    gHost.poisoned = true;
    gHost.ready = false;
    return recordTransport (end == UfsTransferEnd::Timeout ? kIOReturnTimeout : kIOReturnIOError, *ocs);
  }
  gRetried = false;
  if (gResetEvery && !(gRootDiagnostic.command % gResetEvery)) {
    // A periodic stop/start of the request list. The block a 200 ms run died on reads fine on its
    // own, so what accumulates over about 260 commands is controller or request-list state.
    hostResetTransport ();
  }
  traceStep (13);
  // 15 separates the cache maintenance from the response read: a run that stops at 13 died inside
  // cacheInvalidate, one that stops at 15 died reading the response out of the same buffer. Both are
  // CPU touches of memory the controller may still be writing.
  if (gPostDelay) {
    IODelay (gPostDelay);
  }
  if (gNoInvalidate) {
    __asm__ volatile ("dsb sy" ::: "memory");
  } else {
    cacheInvalidate (gHost.virt, kDataOffset + dataBytes);
  }
  traceStep (15);
  gRootDiagnostic.response = loadBe32 (ucdBase () + kRspOffset);
  // 14 separates a stall in the cache/response read from one after the command returned: the
  // deferred run stopped at 13 (50029, command 1) with ocs=00 and scsi=0000, so the first
  // command completed and the thread never reached the caller.
  traceStep (14);
  // The command that wedges never returns, so the page keeps the state left after the previous
  // one. It carries three facts that a photograph has to separate: the controller's own status
  // (is/hcs), and whether the two clock branches the controller sits behind still read as enabled
  // and running - a = bit0 is the AXI branch enable, bit1 its CLK_OFF, g = the same for the
  // aggregate AXI branch. Stage 4 brackets the GCC reads: a run that stops at 4 stalled on the
  // clock controller rather than on the UFS registers.
  static uintptr_t gStatusGcc;
  static char gLastControllerStatus[40];
  const uint32_t is = read32 (gHost.hc, 0x20);
  const uint32_t hcs = read32 (gHost.hc, 0x30);
  traceStep (4);
  if (!gStatusGcc) {
    gStatusGcc = mapRegisters (GCC_BASE, 0x1f0000);
  }
  const uint32_t axi = gStatusGcc ? read32 (gStatusGcc, GCC_UFS_PHY_AXI_ENABLE) : 0xffffffffU;
  const uint32_t agg = gStatusGcc ? read32 (gStatusGcc, GCC_AGGRE_UFS_PHY_AXI_ENABLE) : 0xffffffffU;
  // The reads stay because stage 4 still brackets them - a run that stops at 4 stalled on the clock
  // controller - but the page no longer has room to print them: both have read 1 on every run.
  (void)axi;
  (void)agg;
  // Every eighth command also samples the UFS link itself: PA_LINKSTATE 0x2b is 1 while the link is
  // up and 0 once it dropped, which is the difference between a device that went to sleep under the
  // traffic and a controller that stopped answering on a healthy link.
  // The block being read matters more than the link now: twice with pacing the machine stopped on
  // command 268 and 270 at weights of 100 and 200 ms, so the stop follows the position in the
  // stream rather than the clock, and the last LBA says whether it is a particular block.
  // Single-letter labels keep four fields inside the page's 28-byte strip. n is the transfer size the
  // driver just issued: the stop is fixed at one command, so a size that changes there - a larger
  // read than the buffer was sized for - would explain a controller DMA that runs past its buffer.
  snprintf (gLastControllerStatus, sizeof gLastControllerStatus, "i=%x h=%x b=%x n=%x",
            is, hcs, static_cast<uint32_t> (gRootDiagnostic.lba & 0xffffffffU), dataBytes);
  native_ufs_root_failure = gLastControllerStatus;
  traceStep (14);
  return recordTransport (*ocs == 0 ? kIOReturnSuccess : kIOReturnIOError, *ocs);
}

static bool hostClearUcd (void)
{
  // Pick the slot before anything touches the page: slot 1 rotates every other command when
  // native-ufs-slots=2 is set, so the controller's last UTRD and UCD are not overwritten while it
  // may still be writing them.
  gSlot = gSlotAlternate ? (gSlotSequence++ & 1U) : 0U;
  traceStep (1);
  if (gHost.poisoned) return false;
  // Check before touching the command/response area, not just before ringing.
  const uint32_t doorbellNow = read32 (gHost.hc, 0x58);
  traceStep (2);
  if (doorbellNow != 0) {
    gHost.poisoned = true;
    gHost.ready = false;
    recordTransport (kIOReturnBusy, 15);
    return false;
  }
  traceStep (3);
  if (read32 (gHost.hc, 0x20) & kUfsFatalStatus) {
    gHost.poisoned = true;
    gHost.ready = false;
    recordTransport (kIOReturnIOError, 15);
    return false;
  }
  uint32_t *words = reinterpret_cast<uint32_t *> (ucdBase ());
  for (unsigned index = 0; index < (kPrdtOffset + 16) / 4; index++) {
    words[index] = 0;
  }
  traceStep (4);
  return true;
}

static IOReturn hostProtocolError (uint32_t reason)
{
  gRootDiagnostic.protocol = reason;
  gHost.ready = false;
  gHost.poisoned = true;
  return recordTransport (kIOReturnIOError, gRootDiagnostic.ocs);
}

// Validate a completed response before interpreting zero-filled status bytes.
// Protocol codes: 1 type, 2 task tag, 3 LUN, 4 length/EHS, 5 query echo,
// 6 short/overrun transfer, 7 nonzero NOP result.
static IOReturn hostResponseHeader (uint8_t type, uint8_t lun, uint8_t tag)
{
  const uint8_t *rsp = ucdBase () + kRspOffset;
  if (rsp[0] != type) return hostProtocolError (1);
  if (rsp[3] != tag) return hostProtocolError (2);
  if (rsp[2] != lun) return hostProtocolError (3);
  unsigned length = (unsigned (rsp[10]) << 8) | rsp[11];
  if (rsp[8] || length > 512 - 32) return hostProtocolError (4);
  return kIOReturnSuccess;
}

static IOReturn hostNop (void)
{
  if (!hostClearUcd ()) return kIOReturnNotReady;
  ucdBase ()[3] = 1;
  uint32_t ocs = 15;
  IOReturn result = hostSubmit (0, 0, 200000, &ocs);
  if (result != kIOReturnSuccess) return result;
  result = hostResponseHeader (0x20, 0, 1);
  if (result != kIOReturnSuccess) return result;
  const uint8_t *rsp = ucdBase () + kRspOffset;
  if (rsp[10] || rsp[11]) return hostProtocolError (4);
  if (rsp[6] || rsp[7]) return hostProtocolError (7);
  return kIOReturnSuccess;
}

// Run a SCSI command. direction 0: no data, 1: host -> device from the DMA data area (already filled by the caller), 2: device
// -> host into the DMA data area. Callers other than hostWrite4k never pass direction 1.
static IOReturn hostScsiDir (uint8_t lun, const uint8_t *cdb, unsigned cdbLength, unsigned direction, uint32_t dataBytes,
                             uint32_t timeoutUs, uint32_t *scsiStatus)
{
  *scsiStatus = 0xffff;
  if (!cdb || !cdbLength || cdbLength > 16 || dataBytes > kDataMax || direction > 2 ||
      (dataBytes & 3) || (dataBytes ? !direction : direction)) return kIOReturnBadArgument;
  if (!hostClearUcd ()) return kIOReturnNotReady;
  uint8_t *ucd = ucdBase ();
  ucd[0] = 0x01;                      // COMMAND UPIU
  ucd[1] = direction == 2 ? 0x40 : direction == 1 ? 0x20 : 0x00;   // R: data in, W: data out
  ucd[2] = lun;
  putBe32 (ucd + 12, dataBytes);
  for (unsigned index = 0; index < cdbLength; index++) {
    ucd[16 + index] = cdb[index];
  }
  uint32_t ocs = 0xf;
  IOReturn status = hostSubmit (direction ? direction : 0U, dataBytes, timeoutUs, &ocs);
  if (status != kIOReturnSuccess) return status;
  status = hostResponseHeader (0x21, lun, 0);
  if (status != kIOReturnSuccess) return status;
  const uint8_t *rsp = ucd + kRspOffset;
  *scsiStatus = (uint32_t (rsp[6]) << 8) | rsp[7];
  gRootDiagnostic.scsi = *scsiStatus;
  gRootDiagnostic.residual = loadBe32 (rsp + 12);
  unsigned length = (unsigned (rsp[10]) << 8) | rsp[11];
  if (length) {
    unsigned senseLength = (unsigned (rsp[32]) << 8) | rsp[33];
    if (length < 2 || senseLength > length - 2) return hostProtocolError (4);
    const uint8_t *sense = rsp + 34;
    if (senseLength >= 14 && ((sense[0] & 0x7f) == 0x70 || (sense[0] & 0x7f) == 0x71))
      gRootDiagnostic.sense = (uint32_t (sense[2] & 15) << 16) | (uint32_t (sense[12]) << 8) | sense[13];
    else if (senseLength >= 4 && ((sense[0] & 0x7f) == 0x72 || (sense[0] & 0x7f) == 0x73))
      gRootDiagnostic.sense = (uint32_t (sense[1] & 15) << 16) | (uint32_t (sense[2]) << 8) | sense[3];
  }
  if (!*scsiStatus && ((rsp[1] & 0x60) || gRootDiagnostic.residual)) return hostProtocolError (6);
  IOReturn result = *scsiStatus == 0 ? kIOReturnSuccess : kIOReturnNotReady;
  gRootDiagnostic.result = static_cast<uint32_t> (result);
  return result;
}

// The original read-only entry point: data, when present, always flows device -> host.
static IOReturn hostScsi (uint8_t lun, const uint8_t *cdb, unsigned cdbLength, bool toHost, uint32_t dataBytes,
                          uint32_t timeoutUs, uint32_t *scsiStatus)
{
  if (dataBytes && !toHost) { *scsiStatus = 0xffff; return kIOReturnBadArgument; }
  return hostScsiDir (lun, cdb, cdbLength, dataBytes ? 2U : 0U, dataBytes, timeoutUs, scsiStatus);
}

// Query read request (opcode 0x03 READ ATTRIBUTE / 0x05 READ FLAG). Returns the value field.
static IOReturn hostQueryRead (uint8_t opcode, uint8_t idn, uint32_t *value)
{
  if (!value || (opcode != 3 && opcode != 5)) return kIOReturnBadArgument;
  if (!hostClearUcd ()) return kIOReturnNotReady;
  uint8_t *ucd = ucdBase ();
  ucd[0] = 0x16;                      // QUERY REQUEST
  ucd[3] = 1;
  ucd[5] = 0x01;                      // standard read request (header dword 1, byte 1)
  ucd[12] = opcode;
  ucd[13] = idn;
  uint32_t ocs = 0xf;
  IOReturn status = hostSubmit (0, 0, 200000, &ocs);
  if (status != kIOReturnSuccess) {
    return status;
  }
  status = hostResponseHeader (0x36, 0, 1);
  if (status != kIOReturnSuccess) return status;
  const uint8_t *rsp = ucd + kRspOffset;
  if (rsp[10] || rsp[11]) return hostProtocolError (4);
  if (rsp[12] != opcode || rsp[13] != idn || rsp[14] || rsp[15]) return hostProtocolError (5);
  if (rsp[6] != 0) {
    gRootDiagnostic.result = static_cast<uint32_t> (kIOReturnError);
    return kIOReturnError;
  }
  *value = loadBe32 (rsp + 20);
  return kIOReturnSuccess;
}

// Read `blocks` (<= kDataMax / 4096) 4096-byte blocks of LUN `lun` starting at `lba` into dst.
static IOReturn hostRead4k (uint8_t lun, uint64_t lba, uint32_t blocks, void *dst)
{
  if (!gHost.ready || !dst || !blocks || blocks > kDataMax / 4096 || lba > 0xffffffffULL - (blocks - 1)) {
    return kIOReturnBadArgument;
  }
  // Data commands occasionally wedge the interconnect while every command without a payload
  // returns, so the transfer is issued in small pieces: the DMA burst is 8 KiB at most and the
  // command that stalls (if any) is small enough to be harmless.
  IOLockLock (gHost.lock);
  IOReturn status = kIOReturnSuccess;
  uint8_t *out = static_cast<uint8_t *> (dst);
  // native-ufs-chunk-blocks sets how many blocks one command carries. The default two is what every
  // run so far used; one makes each command half the size, which is the second way to ask whether the
  // stop depends on the transfer size rather than on the position in the stream.
  const uint32_t chunkBlocks = gChunkBlocks ? gChunkBlocks : 2U;
  for (uint32_t done = 0; done < blocks && status == kIOReturnSuccess; done += chunkBlocks) {
    const uint32_t piece = (blocks - done < chunkBlocks) ? blocks - done : chunkBlocks;
    const uint64_t pieceLba = lba + done;
    gRootDiagnostic.lba = pieceLba;
    uint8_t cdb[10] = { 0x28, 0, uint8_t (pieceLba >> 24), uint8_t (pieceLba >> 16),
                        uint8_t (pieceLba >> 8), uint8_t (pieceLba),
                        0, uint8_t (piece >> 8), uint8_t (piece), 0 };
    uint32_t scsi = 0;
    status = hostScsi (lun, cdb, 10, true, piece * 4096U, 1000000, &scsi);
    if (status == kIOReturnSuccess) {
      memcpy (out + done * 4096U, gHost.virt + kDataOffset, piece * 4096U);
    }
  }
  IOLockUnlock (gHost.lock);
  return status;
}

// One READ(10) of at most two 4096-byte blocks that leaves the data in the DMA page instead of
// copying it out. native-ufs-directcpy copies the slice it owns straight from there, so the bounce
// round-trip disappears while the command pattern stays exactly as hostRead4k issues it.
static IOReturn hostReadPiece (uint8_t lun, uint64_t lba, uint32_t blocks)
{
  if (!gHost.ready || !blocks || blocks > 2 || lba > 0xffffffffULL - (blocks - 1)) {
    return kIOReturnBadArgument;
  }
  uint8_t cdb[10] = { 0x28, 0, uint8_t (lba >> 24), uint8_t (lba >> 16), uint8_t (lba >> 8),
                      uint8_t (lba), 0, uint8_t (blocks >> 8), uint8_t (blocks), 0 };
  uint32_t scsi = 0;
  IOLockLock (gHost.lock);
  gRootDiagnostic.lba = lba;
  IOReturn status = hostScsi (lun, cdb, 10, true, blocks * 4096U, 1000000, &scsi);
  IOLockUnlock (gHost.lock);
  return status;
}

// ---- Persistent store and log windows ------------------------------------------------------------------------------
// The only place this driver writes. At most two windows of 4096-byte LBAs on LUN 0 (the Data store and the diagnostic log),
// each opened once by its integration after it has proved that the window lies inside the userdata partition and clear of the
// root image; every write is checked against them again here, so nothing outside them can be written even if a caller is wrong.
static bool gStoreOpen = false, gLogOpen = false;
static uint64_t gStoreFirst = 0, gStoreBlocks = 0, gLogFirst = 0, gLogBlocks = 0;

static bool hostLogOpen (uint64_t first, uint64_t blocks)
{
  if (gLogOpen || !blocks || first > 0xffffffffULL || blocks > 0xffffffffULL - first) return false;
  if (gStoreOpen && first < gStoreFirst + gStoreBlocks && gStoreFirst < first + blocks) return false;
  gLogFirst = first;
  gLogBlocks = blocks;
  gLogOpen = true;
  return true;
}

static bool hostInWindow (uint64_t lba, uint32_t blocks)
{
  if (gStoreOpen && lba >= gStoreFirst && lba - gStoreFirst < gStoreBlocks && blocks <= gStoreBlocks - (lba - gStoreFirst)) return true;
  return gLogOpen && lba >= gLogFirst && lba - gLogFirst < gLogBlocks && blocks <= gLogBlocks - (lba - gLogFirst);
}

static bool hostStoreOpen (uint64_t first, uint64_t blocks)
{
  if (gStoreOpen || !blocks || first > 0xffffffffULL || blocks > 0xffffffffULL - first) return false;
  if (gLogOpen && first < gLogFirst + gLogBlocks && gLogFirst < first + blocks) return false;
  gStoreFirst = first;
  gStoreBlocks = blocks;
  gStoreOpen = true;
  return true;
}

// WRITE(10) of `blocks` 4096-byte blocks from src to lba on LUN 0; the range must lie inside one open window.
static IOReturn hostWrite4k (uint8_t lun, uint64_t lba, uint32_t blocks, const void *src, bool fua = false)
{
  if (!gHost.ready || !src || !blocks || blocks > kDataMax / 4096 || lun != 0 || !hostInWindow (lba, blocks)) {
    return kIOReturnBadArgument;
  }
  IOLockLock (gHost.lock);
  gRootDiagnostic.lba = lba;
  memcpy (gHost.virt + kDataOffset, src, blocks * 4096U);
  uint8_t cdb[10] = { 0x2a, uint8_t (fua ? 0x08 : 0), uint8_t (lba >> 24), uint8_t (lba >> 16), uint8_t (lba >> 8), uint8_t (lba),
                      0, uint8_t (blocks >> 8), uint8_t (blocks), 0 };
  uint32_t scsi = 0;
  IOReturn status = hostScsiDir (0, cdb, 10, 1, blocks * 4096U, 2000000, &scsi);
  IOLockUnlock (gHost.lock);
  return status;
}

// SYNCHRONIZE CACHE(10) for the whole LUN: everything written so far is on the medium when this returns.
static IOReturn hostSyncCache (void)
{
  if (!gHost.ready || (!gStoreOpen && !gLogOpen)) return kIOReturnNotReady;
  IOLockLock (gHost.lock);
  uint8_t cdb[10] = { 0x35 };
  uint32_t scsi = 0;
  IOReturn status = hostScsiDir (0, cdb, 10, 0, 0, 10000000, &scsi);
  IOLockUnlock (gHost.lock);
  return status;
}

