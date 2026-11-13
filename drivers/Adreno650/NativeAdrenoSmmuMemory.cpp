/* SPDX-License-Identifier: MIT */
#include "NativeAdrenoSmmuMemory.h"
static int publishTable(void *cookie,volatile uint64_t *page,size_t bytes)
{
    auto *memory=static_cast<NativeAdrenoMemory *>(cookie);
    if(!memory || !page || bytes!=4096 || ((uintptr_t)page&4095) ||
        (uintptr_t)page<(uintptr_t)memory->mapping())return 1;
    uintptr_t offset=(uintptr_t)page-(uintptr_t)memory->mapping();
    if(offset>memory->bytes() || bytes>memory->bytes()-offset)return 1;
    return memory->publishForDevice(offset,bytes,false)==kIOReturnSuccess?0:1;
}
IOReturn NativeAdrenoBindSmmuVisibility(NativeAdrenoMemory *memory,ArmSmmu4KLiveVisibility *visibility)
{
    if(!visibility)return kIOReturnBadArgument;*visibility={};
    if(!memory || memory->quarantined() || !memory->deviceReferences() || !memory->mapping() ||
        memory->spanCount()!=1 || memory->spans()[0].offset ||
        memory->spans()[0].bytes!=memory->bytes())return kIOReturnBadArgument;
    *visibility={memory,publishTable};return kIOReturnSuccess;
}
/* This port's ArmSmmu4K builds an arena's tables through three callbacks over a page pool, while this driver
 * was written against an arena that named one contiguous mapping and its page count.  The memory object is
 * exactly such a pool: one contiguous span, device-visible, published page by page.  The callbacks below hand
 * out its pages in order and answer the alias questions over the same range, so the driver keeps its own
 * behaviour and the kit keeps its API. */
struct AdrenoArenaPages {
    NativeAdrenoMemory *memory;
    uint64_t next_offset;
};
static int arenaAllocate(void *cookie,uint64_t *physical,volatile uint64_t **mapping)
{
    auto *state=static_cast<AdrenoArenaPages *>(cookie);
    if(!state || !state->memory || !physical || !mapping)return 1;
    NativeAdrenoMemory *memory=state->memory;
    if(state->next_offset>memory->bytes() || memory->bytes()-state->next_offset<4096)return 1;
    uintptr_t base=(uintptr_t)memory->mapping();
    if(!base)return 1;
    void *page=(void *)(base+state->next_offset);
    memset(page,0,4096);
    if(memory->publishForDevice(state->next_offset,4096,false)!=kIOReturnSuccess)return 1;
    *physical=memory->spans()[0].physical+state->next_offset;
    *mapping=reinterpret_cast<volatile uint64_t *>(page);
    state->next_offset+=4096;
    return 0;
}
static volatile uint64_t *arenaFind(void *cookie,uint64_t physical)
{
    auto *state=static_cast<AdrenoArenaPages *>(cookie);
    if(!state || !state->memory)return nullptr;
    uint64_t base=state->memory->spans()[0].physical;
    if(physical<base || physical>=base+state->memory->bytes())return nullptr;
    return reinterpret_cast<volatile uint64_t *>((uintptr_t)state->memory->mapping()+(physical-base));
}
static int arenaOverlaps(void *cookie,uint64_t physical,uint64_t bytes)
{
    auto *state=static_cast<AdrenoArenaPages *>(cookie);
    if(!state || !state->memory)return 0;
    uint64_t base=state->memory->spans()[0].physical;
    return physical+bytes>base && physical<base+state->memory->bytes();
}

IOReturn NativeAdrenoPrepareSmmuMemory(NativeAdrenoMemory *memory,unsigned input_bits,
    unsigned output_bits,uint64_t iova,uint64_t bytes,unsigned upper,size_t budget,
    ArmSmmu4KLiveArena *arena,ArmSmmu4KLiveState *state,ArmSmmu4KLiveVisibility *visibility)
{
    if(!memory || !arena || !state || !visibility)return kIOReturnBadArgument;
    *visibility={};
    if(state->prepared || state->poisoned || memory->quarantined() || memory->deviceReferences())
        return kIOReturnBusy;
    if(!memory->mapping() || memory->spanCount()!=1 ||
        memory->spans()[0].offset || memory->spans()[0].bytes!=memory->bytes() ||
        !memory->bytes() || (memory->bytes()&4095))return kIOReturnUnsupported;
    static AdrenoArenaPages arena_pages;
    static const ArmSmmu4KTablePages arena_table_pages={&arena_pages,arenaAllocate,arenaFind,arenaOverlaps};
    arena_pages.memory=memory;
    arena_pages.next_offset=0;
    *arena={{input_bits,output_bits,memory->spans()[0].physical,budget},iova,bytes,upper,
        &arena_table_pages,nullptr,0,static_cast<volatile uint64_t *>(memory->mapping()),memory->bytes()/4096};
    ArmSmmu4KStatus status=ArmSmmu4KLivePrepare(arena,state);
    if(status!=ArmSmmu4KSuccess)return status==ArmSmmu4KCapacity?kIOReturnNoResources:kIOReturnBadArgument;
    ArmSmmu4KLiveResult audit;
    status=ArmSmmu4KLiveAudit(arena,state,nullptr,0,&audit);
    if(status!=ArmSmmu4KSuccess) {state->poisoned=1;return kIOReturnIOError;}
    IOReturn result=memory->publishForDevice(0,state->tables.table_pages*4096,false);
    if(result==kIOReturnSuccess)result=memory->holdForDevice();
    if(result!=kIOReturnSuccess) {state->poisoned=1;return result;}
    return NativeAdrenoBindSmmuVisibility(memory,visibility);
}
