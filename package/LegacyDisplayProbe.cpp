// SPDX-License-Identifier: BSD-2-Clause-Patent
// Board display probe for instantnoodlep: drives the GopFramebuffer legacy provider with the
// firmware GOP console and answers the diagnostics the component asks for.  Bring-up instrument,
// not a desktop claim.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>   // also declares IOMemoryMap
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSMetaClass.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/protos.h>
#include <machine/machine_routines.h>

extern "C" kmod_info_t g_kernel_kmod_info;
extern "C" void native_legacy_display_static_init(void) __asm__("__GLOBAL__sub_I_NativeLegacyDisplay.cpp");
extern "C" IOService *native_legacy_display_allocate(void);

void native_legacy_display_probe(void);
void native_legacy_display_late_probe(void);

/* --- functional entries ------------------------------------------------- */

void
native_legacy_display_probe(void)
{
	static bool registered;

	printf("DISPLAY: probe entered (registered=%d)\n", registered ? 1 : 0);
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
}

void
native_legacy_display_late_probe(void)
{
	/* Runs after IOKitBSDInit (offline-legacy-start).  The first step is the same probe; the
	 * service publication follows once the object is known to be the original class. */
	native_legacy_display_probe();
}

/* --- diagnostics the component asks for ---------------------------------- */

extern "C" bool native_legacy_prepare_scanout(void) { return false; }
extern "C" bool native_legacy_gop_mapping_observed(IOService *, IOMemoryMap *, uint32_t, uint32_t) { return true; }
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
extern "C" void native_legacy_surface_rejected(uint64_t, uint64_t, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t) {}

/* A slot the owned backend does not implement must not be reached silently. */
extern "C" __attribute__((noreturn, noinline)) void
native_legacy_backend_guard(unsigned slot, void *object, void *caller)
{
	printf("DISPLAY: unimplemented legacy slot %u (object=%p caller=%p)\n", slot, object, caller);
	panic("instantnoodlep legacy display slot %u is not implemented", slot);
}

/* GPU trace and Adreno fences: this board has no GPU driver yet. */
extern "C" int native_gpu_trace_enabled(void) { return 0; }
extern "C" void native_gpu_trace_record(uint32_t, uint32_t, const uint64_t *, uint32_t) {}
extern "C" bool native_adreno_fences(uint32_t *, uint32_t *) { return false; }
extern "C" bool native_adreno_fences_detail(uint32_t *, uint32_t *, uint32_t *) { return false; }