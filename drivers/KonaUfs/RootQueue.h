// SPDX-License-Identifier: BSD-2-Clause-Patent
// Root queue takeover; shared with the host fault-injection harness.
// native_ufs_root_step carries the refusing branch to the boot page: 111-114 and 121-122 here.
// The global is defined before this header is included, inside the anonymous namespace that
// holds the root media code, so no declaration belongs here (declaring one made the name
// ambiguous between the global and a namespace-local copy).

static bool hostWaitRegister (unsigned offset, uint32_t mask, uint32_t value)
{
  uint64_t deadline;
  clock_interval_to_deadline (100, kMillisecondScale, &deadline);
  do {
    if ((read32 (gHost.hc, offset) & mask) == value) return true;
    IODelay (10);
  } while (mach_absolute_time () < deadline);
  return false;
}

static bool hostStopQueue (const char **why)
{
  // Confirm the firmware has no active transfer/task before changing its
  // request list or the storage stream's SMMU routing.
  if (!(read32 (gHost.hc, 0x34) & 1) || (read32 (gHost.hc, 0x30) & 0x0f) != 0x0f) {
    recordTransport (kIOReturnNotReady, 15);
    *why = "controller not ready for takeover";
  native_ufs_root_step = 111;   // why="controller not ready for takeover"
    return false;
  }
  if (read32 (gHost.hc, 0x58) || read32 (gHost.hc, 0x78)) {
    recordTransport (kIOReturnBusy, 15);
    *why = "firmware request outstanding";
  native_ufs_root_step = 112;   // why="firmware request outstanding"
    return false;
  }
  if (read32 (gHost.hc, 0x20) & kUfsFatalStatus) {
    recordTransport (kIOReturnIOError, 15);
    *why = "firmware controller error";
  native_ufs_root_step = 113;   // why="firmware controller error"
    return false;
  }
  hostWrite (0x24, 0);
  if (stopAfter (40051)) { *why = "stop requested"; return false; }
  hostWrite (0x60, 0);
  if (stopAfter (40052)) { *why = "stop requested"; return false; }
  if (!hostWaitRegister (0x60, 1, 0)) {
    recordTransport (kIOReturnTimeout, 15);
    *why = "request list stop";
  native_ufs_root_step = 114;   // why="request list stop"
    return false;
  }
  return true;
}

static bool hostStartQueue (const char **why)
{
  hostWrite (0x50, static_cast<uint32_t> (gHost.phys));
  if (stopAfter (40054)) { *why = "stop requested"; return false; }
  hostWrite (0x54, static_cast<uint32_t> (gHost.phys >> 32));
  if (stopAfter (40055)) { *why = "stop requested"; return false; }
  if (read32 (gHost.hc, 0x50) != uint32_t (gHost.phys) ||
      read32 (gHost.hc, 0x54) != uint32_t (gHost.phys >> 32)) {
    recordTransport (kIOReturnIOError, 15);
    *why = "request list address";
  native_ufs_root_step = 121;   // why="request list address"
    return false;
  }
  if (stopAfter (40056)) { *why = "stop requested"; return false; }
  gRootDiagnostic.isBefore = read32 (gHost.hc, 0x20);
  hostWrite (0x20, gRootDiagnostic.isBefore);
  hostWrite (0x60, 1);
  if (stopAfter (40058)) { *why = "stop requested"; return false; }
  if (!hostWaitRegister (0x60, 1, 1) || !hostWaitRegister (0x30, 3, 3)) {
    recordTransport (kIOReturnTimeout, 15);
    *why = "request list ready";
  native_ufs_root_step = 122;   // why="request list ready"
    return false;
  }
  return true;
}
