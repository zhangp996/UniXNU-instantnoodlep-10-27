// SPDX-License-Identifier: BSD-2-Clause-Patent
// Single-slot completion policy. Port supplies ordered MMIO, invalidate-only
// descriptor reads, a monotonic deadline and bounded pacing. No resubmission.
enum class UfsTransferEnd { Completed, Fatal, Timeout };
struct UfsTransferPoll {
  uint32_t polls = 0, doorbellSeen = 0, interruptSeen = 0, ocs = 15;
};
// 0x71880 covers UFS HCI bits 7, 11, 12, 16, 17 and 18. Bit 24 (0x1000000) is not in the HCI
// list: run U read is=1000000 after a completed command and the very next command never completed,
// so it is treated as fatal from now on - the point is to stop before submitting rather than to
// name the bit.
static constexpr uint32_t kUfsFatalStatus = 0x71880U | 0x1000000U;

template <class Port>
static UfsTransferEnd pollUfsTransfer(Port &port, UfsTransferPoll &state)
{
  for (;;) {
    ++state.polls;
    // 55 means the loop really spins (the MMIO reads inside it return); 54 means the first read
    // after the doorbell never returns.
    port.progress (state.polls);
    uint32_t doorbell = port.doorbell();
    state.doorbellSeen |= doorbell;
    state.interruptSeen |= port.interrupt();
    if (state.interruptSeen & kUfsFatalStatus) return UfsTransferEnd::Fatal;
    if (!(doorbell & 1)) {
      state.ocs = port.completedOcs();
      // A zero doorbell alone does not prove that this command was accepted.
      // Also allow a completion so fast that software never observes DB=1.
      if (state.ocs != 15) return UfsTransferEnd::Completed;
    }
    if (port.expired()) return UfsTransferEnd::Timeout;
    port.delay();
  }
}
