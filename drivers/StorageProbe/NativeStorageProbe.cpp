/*
 * StorageProbe: publish the live IOBlockStorageDevice metadata to the firmware.
 *
 * The storage ABI audit (scripts/audit-storage-interface.py) wants a live observation of
 * the block-storage class. On a board without a serial or USB debug link that observation
 * cannot be captured with a debugger, so this probe writes the facts it can reach into the
 * reserved panic region, where PanicLogDxe prints them on the next boot.
 *
 * Layout at 0x9F410000 (the panic header owns 0x9F400000..0x9F401000):
 *   0x00 magic "STOR"   0x04 version
 *   0x08 metaclass      0x10 instance size
 *   0x18 class name length, 0x1C reserved
 */
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/OSByteOrder.h>
#include <string.h>

#define STORAGE_PROBE_BASE   0x9F410000ULL
#define STORAGE_PROBE_MAGIC  0x53544F52U

struct StorageProbeBlock {
  uint32_t magic;
  uint32_t version;
  uint64_t metaClass;
  uint64_t instanceSize;
  uint32_t nameLength;
  uint32_t reserved;
};

__attribute__((constructor)) static void
storageProbePublish(void)
{
  StorageProbeBlock *block = (StorageProbeBlock *)STORAGE_PROBE_BASE;
  const OSMetaClass *metaClass;

  memset(block, 0, sizeof (*block));
  block->magic = STORAGE_PROBE_MAGIC;

  metaClass = OSMetaClass::getMetaClassWithName(OSSymbol::withCStringNoCopy("IOBlockStorageDevice"));
  if (metaClass == nullptr) {
    block->version = 0;
    return;
  }

  block->version = 1;
  block->metaClass = (uint64_t)(uintptr_t)metaClass;
  block->instanceSize = metaClass->getInstanceSize();
  block->nameLength = 20; /* "IOBlockStorageDevice" */
}
