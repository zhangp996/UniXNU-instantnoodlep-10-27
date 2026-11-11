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
#include <libkern/c++/OSSerialize.h>
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
/* The handle OSMetaClass::preModLoad returns is the fake kext the late registration belongs to; holding it
 * here keeps that kext (and with it our class's metaclass) alive for the life of the kernel. */
static void *gMetaHandle;
static IOService *gSurfaceRoot;
static void publish_display(void);
static void publish_thread(void *, wait_result_t);

/* --- functional entries ------------------------------------------------- */

extern "C" void
native_legacy_display_probe(void)
{
	static bool registered;

	printf("DISPLAY: probe entered (registered=%d)\n", registered ? 1 : 0);
	if (registered) {
		return;
	}
	void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
	printf("DISPLAY: preModLoad(%s)=%p\n", g_kernel_kmod_info.name, handle);
	if (handle && !gMetaHandle) {
		gMetaHandle = handle;
	}
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
		thread_t thread = nullptr;
		if (kernel_thread_start(publish_thread, nullptr, &thread) == KERN_SUCCESS) {
			thread_deallocate(thread);
		}
		unsigned phase = 0;
		PE_parse_boot_argn("offline-legacy-phase", &phase, sizeof(phase));
		printf("DISPLAY: init ok phase=%u\n", phase);
		/* The kernel's offline-mobile-graphics-load probe calls this entry from bsd_init, right after
		 * vfsinit; publishing here would block the boot thread (the IOSurface match waits), which the
		 * storage root cannot afford.  Hand the publication to a kernel thread instead. */
		pendingDisplay = display;
	} else {
		printf("DISPLAY: init failed\n");
		display->release();
	}
}

/* Map the fields the family keeps in the display object: every 8-byte slot that holds a kernel
 * pointer is printed with its offset, so the default-surface field can be recognised once anything
 * writes it.  A fault here panics with the offset already on the console. */
static void
scan_display_fields(IOService *object)
{
	unsigned char *base = (unsigned char *)object;
	unsigned found = 0;
	printf("DISPLAY: field scan of the 0x18a0 object\n");
	for (unsigned off = 0; off + 8 <= 0x18a0 && found < 48; off += 8) {
		uintptr_t value = *(uintptr_t *)(base + off);
		/* The family stores a 0xffffffff00000000 pattern in some slots; it is not a pointer. */
		if (value < 0xfffffff000000000ULL || value >= 0xffffffff00000000ULL) {
			continue;
		}
		uintptr_t vtable = *(uintptr_t *)value;
		printf("  +0x%03x = 0x%llx vtable=0x%llx\n", off, (unsigned long long)value,
		    (unsigned long long)vtable);
		found++;
	}
	printf("DISPLAY: field scan done (%u pointer slots)\n", found);
}

/* Create one BGRA IOSurface through the prelinked IOSurfaceRoot factory (vtable slot 141, the method
 * that keeps the surface count and the tail queue).  Returns null on any failure; every step is logged. */
static OSObject *__attribute__((unused))
create_surface(uint32_t width, uint32_t height, uint32_t stride)
{
	if (!gSurfaceRoot) {
		printf("DISPLAY: no IOSurfaceRoot to create a surface on\n");
		return nullptr;
	}
	OSDictionary *props = OSDictionary::withCapacity(8);
	if (!props) {
		return nullptr;
	}
	uint32_t pixel_format = 0x42475241; /* 'BGRA' */
	uint32_t alloc = (uint32_t)(stride * height);
	struct { const char *key; uint32_t value; } words[] = {
		{"IOSurfaceWidth", width},
		{"IOSurfaceHeight", height},
		{"IOSurfaceBytesPerRow", stride},
		{"IOSurfacePixelFormat", pixel_format},
		{"IOSurfaceAllocSize", alloc},
		{"IOSurfacePixelSizeCastingAllowed", 0},
	};
	for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
		OSData *data = OSData::withBytes(&words[i].value, sizeof(uint32_t));
		if (data) {
			props->setObject(words[i].key, data);
			data->release();
		}
	}
	typedef OSObject *(*create_fn)(void *, void *, OSDictionary *, uint32_t, OSObject **);
	create_fn fn = (create_fn)(*(uintptr_t **)gSurfaceRoot)[141];
	OSObject *out = nullptr;
	printf("DISPLAY: create_surface %ux%u stride %u via vt[141]=%p task=%p\n", width, height, stride,
	    (void *)fn, (void *)current_task());
	OSObject *result = fn(gSurfaceRoot, current_task(), props, 0, &out);
	printf("DISPLAY: create_surface result=%p out=%p\n", (void *)result, (void *)out);
	props->release();
	if (out && out->getMetaClass()) {
		printf("DISPLAY: create_surface class=%s size=0x%x\n", out->getMetaClass()->getClassName(),
		    (unsigned)out->getMetaClass()->getClassSize());
	}
	IOUserClient *client = OSDynamicCast(IOUserClient, out);
	if (client) {
		/* Selector 0 creates a surface: scalarInput carries the parent id, structureInput the
		 * serialized property dictionary, structureOutput the lock result (0xa68 bytes) that tells
		 * the caller where the pixels and the shared state page are. */
		/* Hand-written XML plist: the user client unserializes either XML or binary. */
		char blob[768];
		int blob_len = snprintf(blob, sizeof(blob),
		    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
		    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"
		    "<plist version=\"1.0\"><dict>"
		    "<key>IOSurfaceWidth</key><integer>%u</integer>"
		    "<key>IOSurfaceHeight</key><integer>%u</integer>"
		    "<key>IOSurfaceBytesPerRow</key><integer>%u</integer>"
		    "<key>IOSurfacePixelFormat</key><integer>%u</integer>"
		    "<key>IOSurfaceAllocSize</key><integer>%u</integer>"
		    "</dict></plist>",
		    width, height, stride, pixel_format, alloc);
		bool serialized = blob_len > 0 && blob_len < (int)sizeof(blob);
		uint64_t parent_id = 0;
		uint8_t lock_result[0xa68] = {};
		IOExternalMethodArguments args = {};
		args.selector = 0;
		args.scalarInput = &parent_id;
		args.scalarInputCount = 1;
		args.structureInput = serialized ? blob : nullptr;
		args.structureInputSize = serialized ? (uint32_t)blob_len : 0;
		args.structureOutput = lock_result;
		args.structureOutputSize = sizeof(lock_result);
		IOUserClient2022 *client2022 = (IOUserClient2022 *)client;
		IOReturn rc = client2022->externalMethod(0, (IOExternalMethodArgumentsOpaque *)&args);
		uint32_t *words = (uint32_t *)lock_result;
		printf("DISPLAY: create sel0 rc=0x%x serialized=%d len=%u out=%08x %08x %08x %08x %08x %08x\n",
		    (unsigned)rc, serialized ? 1 : 0, (unsigned)blob_len,
		    words[0], words[1], words[2], words[3], words[4], words[5]);
		if (rc == kIOReturnSuccess) {
			for (unsigned i = 0; i < 16; i++) {
				printf("DISPLAY:   lock[%02u]=0x%llx\n", i, (unsigned long long)((uint64_t *)lock_result)[i]);
			}
		}

	}
	return out;
}

/* Bring-up: an abstract slot the owned backend does not implement yet.  */
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
	if (surface && !gSurfaceRoot) {
		surface->retain();
		gSurfaceRoot = surface;
	}
	if (surface) {
		const uintptr_t *vtable = *(const uintptr_t **)surface;
		const OSMetaClass *meta = surface->getMetaClass();
		printf("DISPLAY: IOSurfaceRoot %p class=%s size=0x%x vtable=%p\n", surface,
		    meta->getClassName(), (unsigned)meta->getClassSize(), (void *)vtable);
		for (unsigned i = 0; i < 8; i++) {
			printf("DISPLAY:   vt[%u]=0x%llx\n", i, (unsigned long long)vtable[i]);
		}
	}
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
	/* The family's own start wants an AppleARMIODevice as provider ("provider was not
	 * AppleARMIODevice"); use one when the prelinked AppleARMPlatform offers it, and keep the plain
	 * IOService as the fallback. */
	IOService *nub = OSDynamicCast(IOService, OSMetaClass::allocClassWithName("AppleARMIODevice"));
	printf("DISPLAY: nub class %s\n", nub ? "AppleARMIODevice" : "IOService (fallback)");
	if (!nub) {
		nub = new IOService;
	}
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
		/* The family's create_default_fb_surface asks IOSurfaceRoot for "PurpleGfxMem", which does not
		 * exist on this board; seed its own region cache at +0x17c0 with system memory and set the
		 * enable byte at +0x1780 so slot 299 builds the temporary surface instead (uke's recipe). */
		IOBufferMemoryDescriptor *surface_region = IOBufferMemoryDescriptor::withCapacity(0x2000000,
		    kIODirectionOutIn);
		if (surface_region) {
			uint32_t format = 0x42475241 /* 'BGRA' */, planes = 1, bpp = 4;
			memset((unsigned char *)display + 0x1700, 0, 0x70);
			memcpy((unsigned char *)display + 0x1700, &format, sizeof(format));
			memcpy((unsigned char *)display + 0x1704, &planes, sizeof(planes));
			memcpy((unsigned char *)display + 0x1708, &bpp, sizeof(bpp));
			((unsigned char *)display)[0x1748] = 0;
			surface_region->retain();
			memcpy((unsigned char *)display + 0x17c0, &surface_region, sizeof(surface_region));
			((unsigned char *)display)[0x1780] |= 1;
			printf("DISPLAY: seeded surface region %p bytes=%llu\n", surface_region,
			    (unsigned long long)surface_region->getLength());
		} else {
			printf("DISPLAY: surface region allocation failed\n");
		}
		bool dispAttached = display->attach(nub);
		bool dispStarted = dispAttached ? display->start(nub) : false;
		printf("DISPLAY: display attach=%d start=%d\n", dispAttached, dispStarted);
		if (dispStarted) {
			scan_display_fields(display);
			/* Adopt the temporary surface the family created at +0x1620 as the default one at +0x1618,
			 * the way the uke port does; retain the region and descriptor so the compositor's mapping
			 * never points at freed pages when the display's stop clears the field. */
			void *temp = nullptr, *def = nullptr;
			memcpy(&temp, (unsigned char *)display + 0x1620, sizeof(temp));
			memcpy(&def, (unsigned char *)display + 0x1618, sizeof(def));
			printf("DISPLAY: surfaces default=%p temp=%p\n", def, temp);
			if (!def && temp) {
				void *surface_region = nullptr, *surface_descriptor = nullptr;
				uint32_t sid = 0;
				memcpy(&sid, (unsigned char *)temp + 0x10, sizeof(sid));
				memcpy(&surface_region, (unsigned char *)temp + 0x28, sizeof(surface_region));
				memcpy(&surface_descriptor, (unsigned char *)temp + 0x30, sizeof(surface_descriptor));
				if (surface_region) {
					((OSObject *)surface_region)->retain();
				}
				if (surface_descriptor) {
					((OSObject *)surface_descriptor)->retain();
				}
				((OSObject *)temp)->retain();
				memcpy((unsigned char *)display + 0x1618, &temp, sizeof(temp));
				printf("DISPLAY: adopted default surface %p sid=%u region=%p descriptor=%p\n",
				    temp, (unsigned)sid, surface_region, surface_descriptor);
			}
		}
	}
	if (memory) memory->release();
	if (ranges) ranges->release();
}

static void
publish_thread(void *, wait_result_t)
{
	IOSleep(2000);
	publish_display();
}

extern "C" void
native_legacy_display_late_probe(void)
{
	/* offline-legacy-start runs at bsd_autoconf, before the family is loaded. The load itself is
	 * done by offline-mobile-graphics-load (offline-component-load-probes.patch), which then calls
	 * native_legacy_display_probe(); this entry only records that ordering. */
	printf("DISPLAY: late probe (load handled by offline-mobile-graphics-load)\n");
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
extern "C" void native_legacy_swap_validation_observed(bool accepted, unsigned reason, const void *request,
    const void *surface)
{
	static unsigned logged;
	if (logged < 6) {
		logged++;
		uint64_t surfaces[4] = {};
		if (request) {
			memcpy(surfaces, (const unsigned char *)request + 2624, sizeof(surfaces));
		}
		printf("DISPLAY: swap accepted=%d reason=%u request=%p surface=%p req_surfaces=%llx,%llx,%llx,%llx\n",
		    accepted ? 1 : 0, reason, request, surface,
		    (unsigned long long)surfaces[0], (unsigned long long)surfaces[1],
		    (unsigned long long)surfaces[2], (unsigned long long)surfaces[3]);
	}
}

/* Diagnostic hooks the display component calls but this board does not use yet. */
extern "C" void native_legacy_copy_timing_observed(uint64_t, uint64_t) {}
extern "C" void native_legacy_frame_colors_observed(const uint32_t *, const uint64_t *, uint64_t) {}
extern "C" void native_legacy_frame_observed(unsigned, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {}
extern "C" void native_legacy_surface_rejected(uint64_t, uint64_t, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t) {}
extern "C" void native_legacy_transfer_observed(IOReturn, const void *, uint64_t) {}
extern "C" void native_legacy_vsync_observed(unsigned, unsigned) {}
/* Abstract-slot guard: the component declares it noreturn, so it must not return. */
extern "C" __attribute__((noreturn, noinline)) void
native_legacy_backend_guard(unsigned slot, void *object, void *caller)
{
	printf("DISPLAY: abstract slot %u (object=%p caller=%p); panicking\n", slot, object, caller);
	panic("instantnoodlep legacy display abstract slot %u", slot);
}

extern "C" int native_gpu_trace_enabled(void) { return 0; }
extern "C" void native_gpu_trace_record(uint32_t, uint32_t, const uint64_t *, uint32_t) {}
extern "C" bool native_adreno_fences(uint32_t *, uint32_t *) { return false; }
extern "C" bool native_adreno_fences_detail(uint32_t *, uint32_t *, uint32_t *) { return false; }
