// SPDX-License-Identifier: BSD-2-Clause-Patent
// The verified block bridge for this board's storage component.
//
// On arm64 the bridge belongs to the block driver: the kernel owner-root code asks
// native_verified_block_root_authorized() about a dev_t and native_verified_block_volume_device()
// about a signed volume, and the driver answers after checking its own signed policy.
// VirtioMmioBlock carries that bridge beside its transport; this board links ShengUfs alone, which
// exports the authentication hook native_ufs_block_authenticates() but not the bridge itself.

#include <IOKit/IOBSD.h>
#include <IOKit/IOService.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/crypto/sha2.h>
#include <kern/debug.h>
#include <sys/param.h>
#include <sys/types.h>

extern "C" {
// Provided by ShengUfs when it is linked with the owner policy: -1 is not a UFS window, else 1 or 0
// for the signed volume UUID.
int native_ufs_block_authenticates(IOService *provider, const char *uuid, uint8_t *digest)
    __attribute__((weak));
// Nothing in this build runs the boot progress layer, and the verified read path calls this weak
// hook for every verified block.
void boot_progress_poll(void);
}

void boot_progress_poll(void)
{
}

// Ask the storage component whether the media behind a device carries the signed policy for its
// volume, optionally as one specific UUID. Returns 1 when it does, 0 otherwise.
static int bridgeQuery(dev_t device, const char *expected, uint8_t *digest)
{
    if (!native_ufs_block_authenticates) {
        return 0;
    }
    OSDictionary *matching = IOService::serviceMatching("IOMedia");
    OSDictionary *properties = OSDictionary::withCapacity(2);
    OSNumber *majorNumber = OSNumber::withNumber(static_cast<unsigned>(major(device)), 32);
    OSNumber *minorNumber = OSNumber::withNumber(static_cast<unsigned>(minor(device)), 32);
    if (!matching || !properties || !majorNumber || !minorNumber) {
        OSSafeReleaseNULL(minorNumber);
        OSSafeReleaseNULL(majorNumber);
        OSSafeReleaseNULL(properties);
        OSSafeReleaseNULL(matching);
        return 0;
    }
    bool ready = properties->setObject(kIOBSDMajorKey, majorNumber) &&
                 properties->setObject(kIOBSDMinorKey, minorNumber) &&
                 matching->setObject("IOPropertyMatch", properties);
    minorNumber->release();
    majorNumber->release();
    properties->release();
    OSIterator *iterator = ready ? IOService::getMatchingServices(matching) : nullptr;
    matching->release();
    if (!iterator) {
        return 0;
    }
    bool authenticated = false;
    unsigned matches = 0;
    while (OSObject *object = iterator->getNextObject()) {
        IOService *media = OSDynamicCast(IOService, object);
        if (!media) {
            continue;
        }
        // One media per BSD device; two of them make the identity ambiguous and authorize nothing.
        if (++matches != 1) {
            authenticated = false;
            break;
        }
        OSString *uuid = OSDynamicCast(OSString, media->getProperty("UUID"));
        if (!uuid || uuid->getLength() != 36 ||
            (expected && strcmp(expected, uuid->getCStringNoCopy()))) {
            continue;
        }
        unsigned depth = 0;
        for (IOService *provider = media; provider && depth < 64; provider = provider->getProvider()) {
            ++depth;
            int result = native_ufs_block_authenticates(provider, uuid->getCStringNoCopy(), digest);
            if (result >= 0) {
                authenticated = result == 1;
                break;
            }
        }
    }
    iterator->release();
    if (!authenticated) {
        if (digest) {
            bzero(digest, SHA384_DIGEST_LENGTH);
        }
        return 0;
    }
    return 1;
}

extern "C" int native_verified_block_root_authorized(dev_t device)
{
    return bridgeQuery(device, nullptr, nullptr);
}

extern "C" int native_verified_block_manifest_sha384(dev_t device, const char *uuid, uint8_t digest[48])
{
    if (!digest) {
        return 0;
    }
    bzero(digest, SHA384_DIGEST_LENGTH);
    if (!uuid || strlen(uuid) != 36) {
        return 0;
    }
    return bridgeQuery(device, uuid, digest);
}

// Resolve a signed volume UUID to its BSD device and path, and only when that media is authorized.
extern "C" int native_verified_block_volume_device(const char *uuid, dev_t *device, char *path,
                                                   size_t size)
{
    if (!uuid || !device || !path || size < 6 || strlen(uuid) != 36) {
        return 0;
    }
    *device = NODEV;
    path[0] = 0;
    OSDictionary *matching = IOService::serviceMatching("IOMedia");
    OSDictionary *properties = OSDictionary::withCapacity(1);
    OSString *wanted = OSString::withCString(uuid);
    if (!matching || !properties || !wanted) {
        OSSafeReleaseNULL(wanted);
        OSSafeReleaseNULL(properties);
        OSSafeReleaseNULL(matching);
        return 0;
    }
    bool ready = properties->setObject("UUID", wanted) &&
                 matching->setObject("IOPropertyMatch", properties);
    wanted->release();
    properties->release();
    OSIterator *iterator = ready ? IOService::getMatchingServices(matching) : nullptr;
    matching->release();
    if (!iterator) {
        return 0;
    }
    unsigned matches = 0;
    bool authenticated = false;
    dev_t candidate = NODEV;
    char candidatePath[64] = {};
    while (OSObject *object = iterator->getNextObject()) {
        IOService *media = OSDynamicCast(IOService, object);
        if (!media) {
            continue;
        }
        if (++matches != 1) {
            authenticated = false;
            break;
        }
        OSString *identity = OSDynamicCast(OSString, media->getProperty("UUID"));
        OSString *name = OSDynamicCast(OSString, media->getProperty(kIOBSDNameKey));
        OSNumber *majorNumber = OSDynamicCast(OSNumber, media->getProperty(kIOBSDMajorKey));
        OSNumber *minorNumber = OSDynamicCast(OSNumber, media->getProperty(kIOBSDMinorKey));
        if (!identity || strcmp(identity->getCStringNoCopy(), uuid) || !name || !majorNumber ||
            !minorNumber || !name->getLength() || name->getLength() >= sizeof(candidatePath) - 6 ||
            name->getLength() >= size - 6 || majorNumber->unsigned64BitValue() > 255 ||
            minorNumber->unsigned64BitValue() > 0xffffff) {
            continue;
        }
        dev_t found = static_cast<dev_t>(makedev(majorNumber->unsigned32BitValue(),
                                                 minorNumber->unsigned32BitValue()));
        if (!native_verified_block_root_authorized(found)) {
            continue;
        }
        snprintf(candidatePath, sizeof(candidatePath), "/dev/%s", name->getCStringNoCopy());
        candidate = found;
        authenticated = true;
    }
    iterator->release();
    if (!authenticated || matches != 1) {
        return 0;
    }
    *device = candidate;
    strlcpy(path, candidatePath, size);
    return 1;
}

// One-shot report for the owner cache mount: the kernel asks
// native_verified_block_volume_device() for the SystemOS volume, and this says whether that lookup
// failed, which media the registry holds, and how far the mount got. Panics so the panel shows it.
extern "C" void qcom_owner_report(void);
extern "C" int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_arg);

extern "C" void qcom_owner_report(void)
{
    int report = 0;
    if (!PE_parse_boot_argn("native-owner-report", &report, sizeof(report)) || report != 1) {
        return;
    }
    extern volatile int native_owner_cache_stage;
    extern volatile int native_owner_cache_status;
    extern volatile int native_owner_cache_mount_status;
    extern volatile int native_owner_cache_authorized;
    extern char native_owner_cache_device_path[64];
    char media[448];
    size_t used = 0;
    OSDictionary *matching = IOService::serviceMatching("IOMedia");
    OSIterator *iterator = matching ? IOService::getMatchingServices(matching) : nullptr;
    unsigned count = 0;
    while (iterator && used + 80 < sizeof(media) && count++ < 5) {
        IOService *entry = OSDynamicCast(IOService, iterator->getNextObject());
        if (!entry) {
            break;
        }
        OSString *name = OSDynamicCast(OSString, entry->getProperty(kIOBSDNameKey));
        OSString *uuid = OSDynamicCast(OSString, entry->getProperty("UUID"));
        used += snprintf(media + used, sizeof(media) - used, "\n%s %s",
                         name ? name->getCStringNoCopy() : "-",
                         uuid ? uuid->getCStringNoCopy() : "-");
    }
    OSSafeReleaseNULL(iterator);
    OSSafeReleaseNULL(matching);
    panic("OWNER CACHE REPORT stage=%d status=%d mount=%d auth=%d dev=%.24s%s",
          native_owner_cache_stage, native_owner_cache_status, native_owner_cache_mount_status,
          native_owner_cache_authorized, native_owner_cache_device_path, media);
}

// A once-per-five-seconds live trace on the console: stage counters from the owner, storage and RAM
// block paths. Without it a boot that neither panics nor prints cannot be told apart from a hang.
extern "C" void qcom_owner_heartbeat(void);
extern "C" int kernel_thread_start(void (*)(void *, wait_result_t), void *, thread_t *);
extern "C" void thread_deallocate(thread_t);

static void heartbeat_thread(void *, wait_result_t)
{
    extern volatile int native_owner_cache_stage;
    extern volatile int native_owner_cache_mount_status;
    extern volatile int native_owner_data_stage;
    extern volatile int native_owner_data_status;
    extern volatile int native_ufs_root_step;
    extern volatile uint32_t native_ufs_verified_blocks;
    extern volatile uint32_t native_ram_block_ready;
    extern volatile uint64_t native_ram_block_bytes;
    for (;;) {
        IOSleep(5000);
        kprintf("HEARTBEAT cache=%d/%d data=%d/%d root=%d verified=%u ram=%u/%llu\n",
                native_owner_cache_stage, native_owner_cache_mount_status,
                native_owner_data_stage, native_owner_data_status,
                native_ufs_root_step, native_ufs_verified_blocks,
                native_ram_block_ready, native_ram_block_bytes);
    }
}

extern "C" void qcom_owner_heartbeat(void)
{
    thread_t thread = nullptr;
    if (kernel_thread_start(heartbeat_thread, nullptr, &thread) == KERN_SUCCESS) {
        thread_deallocate(thread);
    }
}

