// SPDX-License-Identifier: BSD-2-Clause-Patent
enum : uint32_t {
  kUcdOffset = 4096,
  // native-ufs-slots=2 alternates the command block per command: one 4 KiB page per slot, so the
  // CPU never rewrites the UTRD/UCD the controller has just used. Slot 0 keeps the addresses every
  // earlier run used, so the switch off is byte-for-byte the old driver.
  kUcdStride = 4096,
  kUcdSlots = 2,
  kRspOffset = 512,
  kPrdtOffset = 1024,
  kDataOffset = kUcdOffset + kUcdStride * kUcdSlots,
  kDataMax = 131072,
  kDmaBytes = kDataOffset + kDataMax,
};
