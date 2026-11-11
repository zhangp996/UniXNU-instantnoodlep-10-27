// SPDX-License-Identifier: BSD-2-Clause-Patent
// Board display probe for instantnoodlep: drives the GopFramebuffer legacy provider with the
// firmware GOP console and answers the diagnostics the component asks for.  Bring-up instrument,
// not a desktop claim.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>   // also declares IOMemoryMap
#include <IOKit/IOUserClient.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSKext.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/protos.h>
#include <machine/machine_routines.h>

extern "C" kmod_info_t g_kernel_kmod_info;
extern "C" void native_legacy_display_static_init(void) __asm__("__GLOBAL__sub_I_NativeLegacyDisplay.cpp");
extern "C" IOService *native_legacy_display_allocate(void);

extern "C" void native_legacy_display_probe(void);
extern "C" void native_legacy_display_late_probe(void);
extern "C" OSReturn OSKextLoadKextWithIdentifier(const char *);
static IOService *pendingDisplay;

/* --- functional entries ------------------------------------------------- */

extern "C" void
native_legacy_display_probe(void)
{
	static bool family_loaded;
	static bool registered;

	printf("DISPLAY: probe entered (registered=%d)\n", registered ? 1 : 0);
	if (!family_loaded) {
		/* The family carries no OSBundleRequired, so the prelinker leaves it to be loaded on
		 * demand; nothing in this port asks for it yet. */
		OSReturn rc = OSKext::loadKextWithIdentifier("com.apple.iokit.IOMobileGraphicsFamily");
		printf("DISPLAY: load family kext rc=0x%x loaded=%d\n", (unsigned)rc,
		    OSKext::isKextWithIdentifierLoaded("com.apple.iokit.IOMobileGraphicsFamily") ? 1 : 0);
		family_loaded = true;
	}
	if (registered) {
		return;
	}
	void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
	if (!handle) {
		printf("DISPLAY: preModLoad failed\n");
		return;
	}
	native_legacy_display_static_init();
	kern_return_t status = OSMetaClass::postModLoad(handle);
	if (status) {
		printf("DISPLAY: postModLoad failed %d\n", status);
		return;
	}
	registered = true;

	PE_Video video = {};
	if (PE_current_console(&video) || !video.v_baseAddr || !video.v_width || !video.v_height ||
	    video.v_depth != 32 || video.v_rotate || video.v_rowBytes < video.v_width * 4) {
		printf("DISPLAY: no usable GOP console (base=0x%llx %lux%lu depth=%lu rot=%lu stride=%llu)\n",
		    (unsigned long long)video.v_baseAddr, (unsigned long)video.v_width, (unsigned long)video.v_height,
		    (unsigned long)video.v_depth, (unsigned long)video.v_rotate, (unsigned long long)video.v_rowBytes);
		return;
	}
	printf("DISPLAY: GOP console 0x%llx %lux%lu depth=%lu stride=%llu\n",
	    (unsigned long long)video.v_baseAddr, (unsigned long)video.v_width, (unsigned long)video.v_height,
	    (unsigned long)video.v_depth, (unsigned long long)video.v_rowBytes);

	IOService *display = native_legacy_display_allocate();
	if (!display) {
		printf("DISPLAY: allocate failed\n");
		return;
	}
	printf("DISPLAY: allocated %s size=0x%x legacy=%d mobilefb=%d\n",
	    display->getMetaClass()->getClassName(),
	    (unsigned)display->getMetaClass()->getClassSize(),
	    display->metaCast("IOMobileFramebufferLegacy") ? 1 : 0,
	    display->metaCast("IOMobileFramebuffer") ? 1 : 0);
	{
		extern char boundParentMeta[] __asm__("__ZN25IOMobileFramebufferLegacy10gMetaClassE");
		const uint64_t *raw = (const uint64_t *)(void *)boundParentMeta;
		printf("DISPLAY: bound parent meta=%p raw=%llx %llx %llx %llx\n", (void *)raw,
		    (unsigned long long)raw[0], (unsigned long long)raw[1],
		    (unsigned long long)raw[2], (unsigned long long)raw[3]);
		const OSSymbol *name = OSSymbol::withCString("IOMobileFramebufferLegacy");
		const OSMetaClass *registered = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
		printf("DISPLAY: registered family meta=%p name=%s size=0x%x\n", (void *)registered,
		    registered ? registered->getClassName() : "-",
		    registered ? (unsigned)registered->getClassSize() : 0);
		if (name) name->release();
	}
	{
		const OSMetaClass *mc = display->getMetaClass();
		for (int depth = 0; mc && depth < 8; ++depth) {
			printf("DISPLAY: meta[%d] %s size=0x%x super=%s\n", depth,
			    mc->getClassName(), (unsigned)mc->getClassSize(),
			    mc->getSuperClass() ? mc->getSuperClass()->getClassName() : "-");
			mc = mc->getSuperClass();
		}
	}
	if (display->init()) {
		unsigned phase = 0;
		PE_parse_boot_argn("offline-legacy-phase", &phase, sizeof(phase));
		printf("DISPLAY: init ok phase=%u, retained for publication\n", phase);
		pendingDisplay = display;
	} else {
		printf("DISPLAY: init failed\n");
		display->release();
	}
}

static void
publish_display(void)
{
	IOService *display = pendingDisplay;
	if (!display) {
		printf("DISPLAY: nothing pending to publish\n");
		return;
	}
	pendingDisplay = nullptr;
	OSReturn rc = OSKextLoadKextWithIdentifier("com.apple.iokit.IOSurface");
	printf("DISPLAY: IOSurface kext rc=0x%x\n", (unsigned)rc);
	OSDictionary *matching = OSDictionary::withCapacity(1);
	const OSObject *names[2] = {OSString::withCString("IOCoreSurfaceRoot"), OSString::withCString("IOSurfaceRoot")};
	OSArray *list = (names[0] && names[1]) ? OSArray::withObjects(names, 2) : nullptr;
	if (matching && list) {
		matching->setObject(gIONameMatchKey, list);
	}
	IOService *surface = matching ? IOService::waitForMatchingService(matching, 5000000000ULL) : nullptr;
	printf("DISPLAY: IOSurface root found=%d\n", surface ? 1 : 0);
	if (surface) surface->release();
	if (list) list->release();
	for (int i = 0; i < 2; ++i) if (names[i]) ((OSObject *)names[i])->release();
	if (matching) matching->release();

	PE_Video video = {};
	if (PE_current_console(&video) || !video.v_baseAddr || !video.v_depth) {
		printf("DISPLAY: no GOP console at publication\n");
		return;
	}
	IOService *parent = IOService::getResourceService();
	IOService *nub = new IOService;
	IODeviceMemory *memory = IODeviceMemory::withRange(video.v_baseAddr, video.v_rowBytes * video.v_height);
	OSArray *ranges = OSArray::withCapacity(1);
	if (nub && memory && ranges && parent && nub->init()) {
		ranges->setObject(memory);
		nub->setDeviceMemory(ranges);
		nub->setName("NativeGOPFramebuffer");
		bool attached = nub->attach(parent);
		bool started = attached ? nub->start(parent) : false;
		printf("DISPLAY: nub %p attach=%d start=%d mem=0x%llx+0x%llx\n", nub, attached, started,
		    (unsigned long long)video.v_baseAddr,
		    (unsigned long long)(video.v_rowBytes * video.v_height));
		IORegistryEntry *dtParent = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
		const char type[] = "display-subsystem";
		OSData *data = OSData::withBytes(type, sizeof(type));
		if (dtParent && data && nub->setProperty("device_type", data)) {
			nub->setName("gop-display", gIODTPlane);
			printf("DISPLAY: dt attach=%d\n", nub->attachToParent(dtParent, gIODTPlane) ? 1 : 0);
		}
		if (data) data->release();
		if (dtParent) dtParent->release();
		bool dispAttached = display->attach(nub);
		bool dispStarted = dispAttached ? display->start(nub) : false;
		printf("DISPLAY: display attach=%d start=%d\n", dispAttached, dispStarted);
	}
	if (memory) memory->release();
	if (ranges) ranges->release();
}

static void
display_retry(void *, wait_result_t)
{
	/* offline-legacy-start runs at bsd_autoconf, before IOKit has settled; give the registry a
	 * moment, then register the owned class and publish it on the firmware framebuffer. */
	IOSleep(5000);
	printf("DISPLAY: retry after 5 s\n");
	native_legacy_display_probe();
	publish_display();
}

extern "C" void
native_legacy_display_late_probe(void)
{
	thread_t thread = nullptr;
	if (kernel_thread_start(display_retry, nullptr, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}

/* --- diagnostics the component asks for ---------------------------------- */

extern "C" bool native_legacy_prepare_scanout(void) { return false; }
static bool
publish_word(IORegistryEntry *entry, const char *name, uint32_t value)
{
	OSData *existing = OSDynamicCast(OSData, entry->getProperty(name));
	if (existing) {
		uint32_t current = 0;
		if (existing->getLength() == sizeof(current)) {
			memcpy(&current, existing->getBytesNoCopy(), sizeof(current));
		}
		return current == value;
	}
	OSData *data = OSData::withBytes(&value, sizeof(value));
	bool ok = data && entry->setProperty(name, data);
	if (data) {
		data->release();
	}
	return ok;
}

/* The component calls this when it maps the provider's framebuffer.  The firmware GOP mode and the
 * UI scale become the published display geometry, as the reference board does at phase 4. */
extern "C" bool
native_legacy_gop_mapping_observed(IOService *object, IOMemoryMap *mapping, uint32_t width, uint32_t height)
{
	unsigned scale = 0;
	PE_parse_boot_argn("offline-legacy-unit-scale", &scale, sizeof(scale));
	if (!scale) {
		return true;
	}
	unsigned phase = 0;
	PE_parse_boot_argn("offline-legacy-phase", &phase, sizeof(phase));
	PE_Video video = {};
	if (scale < 1 || scale > 3 || phase != 4 || PE_current_console(&video) ||
	    video.v_width != width || video.v_height != height || video.v_rotate ||
	    !mapping || mapping->getPhysicalAddress() != video.v_baseAddr) {
		printf("DISPLAY: mapping check refused scale=%u phase=%u video=%lux%lu mapping=%p\n",
		    scale, phase, (unsigned long)video.v_width, (unsigned long)video.v_height, (void *)mapping);
		return false;
	}
	IORegistryEntry *chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
	if (!chosen) {
		printf("DISPLAY: no /chosen node\n");
		return false;
	}
	bool ok = publish_word(chosen, "display-scale", scale) &&
	    publish_word(chosen, "display-rotation", video.v_rotate);
	printf("DISPLAY: chosen display-scale=%u rotation=%lu ok=%d\n", scale,
	    (unsigned long)video.v_rotate, ok ? 1 : 0);
	chosen->release();
	return ok;
}
extern "C" bool native_legacy_gpu_offer(void *, IOMemoryDescriptor *, uint64_t, uint32_t, uint32_t, uint32_t) { return false; }
extern "C" void native_legacy_gpu_fallback_observed(int) {}
extern "C" void native_legacy_pm_observed(unsigned, IOReturn, unsigned long) {}
extern "C" void native_legacy_registration_observed(IOService *) {}
extern "C" void native_legacy_retirement_observed(IOService *) {}
extern "C" void native_legacy_client_enter(task_t, uint32_t) {}
extern "C" void native_legacy_client_observed(IOReturn, IOUserClient *) {}
extern "C" void native_legacy_swap_stage(unsigned) {}
extern "C" void native_legacy_swap_validation_observed(bool, unsigned, const void *, const void *) {}
extern "C" void native_legacy_transfer_observed(IOReturn, const void *, uint64_t) {}
extern "C" void native_legacy_copy_timing_observed(uint64_t, uint64_t) {}
extern "C" void native_legacy_frame_observed(unsigned, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {}
extern "C" void native_legacy_frame_colors_observed(const uint32_t *, const uint64_t *, uint64_t) {}
extern "C" void native_legacy_vsync_observed(unsigned, unsigned) {}
extern "C" void native_legacy_surface_rejected(uint64_t, uint64_t, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t) {}

/* Bring-up: an abstract slot the owned backend does not implement yet.  Record the first few
 * hits and return instead of taking the machine down, so the boot can show the next wall. */
extern "C" __attribute__((noinline)) void
native_legacy_backend_guard(unsigned slot, void *object, void *caller)
{
	static unsigned guard_calls;
	if (guard_calls < 8) {
		guard_calls++;
		printf("DISPLAY: unimplemented legacy slot %u (object=%p caller=%p), returning\n",
		    slot, object, caller);
	}
}

/* GPU trace and Adreno fences: this board has no GPU driver yet. */
extern "C" int native_gpu_trace_enabled(void) { return 0; }
extern "C" void native_gpu_trace_record(uint32_t, uint32_t, const uint64_t *, uint32_t) {}
extern "C" bool native_adreno_fences(uint32_t *, uint32_t *) { return false; }
extern "C" bool native_adreno_fences_detail(uint32_t *, uint32_t *, uint32_t *) { return false; }