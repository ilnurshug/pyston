//
// Created by user on 7/18/15.
//

#include "MarkSweepGC.h"

void *pyston::gc::MarkSweepGC::gc_alloc(size_t bytes, pyston::gc::GCKind kind_id) {
#if EXPENSIVE_STAT_TIMERS
    // This stat timer is quite expensive, not just because this function is extremely hot,
    // but also because it tends to increase the size of this function enough that we can't
    // inline it, which is especially useful for this function.
    ScopedStatTimer gc_alloc_stattimer(gc_alloc_stattimer_counter, 15);
#endif
    size_t alloc_bytes = bytes + sizeof(GCAllocation);

#ifndef NVALGRIND
// Adding a redzone will confuse the allocator, so disable it for now.
#define REDZONE_SIZE 0
// This can also be set to "RUNNING_ON_VALGRIND", which will only turn on redzones when
// valgrind is actively running, but I think it's better to just always turn them on.
// They're broken and have 0 size anyway.
#define ENABLE_REDZONES 1

    if (ENABLE_REDZONES)
        alloc_bytes += REDZONE_SIZE * 2;
#endif

    GCAllocation* alloc = global_heap.alloc(alloc_bytes);

#ifndef NVALGRIND
    VALGRIND_DISABLE_ERROR_REPORTING;
#endif

    alloc->kind_id = kind_id;
    alloc->gc_flags = 0;

    if (kind_id == GCKind::CONSERVATIVE || kind_id == GCKind::PRECISE) {
        // Round the size up to the nearest multiple of the pointer width, so that
        // we have an integer number of pointers to scan.
        // TODO We can probably this better; we could round down when we scan, or even
        // not scan this at all -- a non-pointerwidth-multiple allocation seems to mean
        // that it won't be storing pointers (or it will be storing them non-aligned,
        // which we don't support).
        bytes = (bytes + sizeof(void*) - 1) & (~(sizeof(void*) - 1));
        assert(bytes < (1 << 31));
        alloc->kind_data = bytes;
    }

    void* r = alloc->user_data;

#ifndef NVALGRIND
    VALGRIND_ENABLE_ERROR_REPORTING;

    if (ENABLE_REDZONES) {
        r = ((char*)r) + REDZONE_SIZE;
    }
    VALGRIND_MALLOCLIKE_BLOCK(r, bytes, REDZONE_SIZE, false);
#endif

    // TODO This doesn't belong here (probably in PythonGCObject?)...
    if (kind_id == GCKind::PYTHON) {
        ((Box*)r)->cls = NULL;
    }

#ifndef NDEBUG
// I think I have a suspicion: the gc will see the constant and treat it as a
// root.  So instead, shift to hide the pointer
// if ((((intptr_t)r) >> 4) == (0x127014f9f)) {
// raise(SIGTRAP);
//}

// if (VERBOSITY()) printf("Allocated %ld bytes at [%p, %p)\n", bytes, r, (char*)r + bytes);
#endif

#if STAT_ALLOCATIONS
    gc_alloc_bytes.log(bytes);
    gc_alloc_bytes_typed[(int)kind_id].log(bytes);
#endif

    return r;
}

void *pyston::gc::MarkSweepGC::gc_realloc(void *ptr, size_t bytes) {
    // Normal realloc() supports receiving a NULL pointer, but we need to know what the GCKind is:
    assert(ptr);

    size_t alloc_bytes = bytes + sizeof(GCAllocation);

    GCAllocation* alloc;
    void* rtn;

#ifndef NVALGRIND
    if (ENABLE_REDZONES) {
        void* base = (char*)ptr - REDZONE_SIZE;
        alloc = global_heap.realloc(GCAllocation::fromUserData(base), alloc_bytes + 2 * REDZONE_SIZE);
        void* rtn_base = alloc->user_data;
        rtn = (char*)rtn_base + REDZONE_SIZE;
    } else {
        alloc = global_heap.realloc(GCAllocation::fromUserData(ptr), alloc_bytes);
        rtn = alloc->user_data;
    }

    VALGRIND_FREELIKE_BLOCK(ptr, REDZONE_SIZE);
    VALGRIND_MALLOCLIKE_BLOCK(rtn, alloc_bytes, REDZONE_SIZE, true);
#else
    alloc = global_heap.realloc(GCAllocation::fromUserData(ptr), alloc_bytes);
    rtn = alloc->user_data;
#endif

    if (alloc->kind_id == GCKind::CONSERVATIVE || alloc->kind_id == GCKind::PRECISE) {
        bytes = (bytes + sizeof(void*) - 1) & (~(sizeof(void*) - 1));
        assert(bytes < (1 << 31));
        alloc->kind_data = bytes;
    }

#if STAT_ALLOCATIONS
    gc_alloc_bytes.log(bytes);
#endif

    return rtn;
}

void pyston::gc::MarkSweepGC::gc_free(void *ptr) {
    assert(ptr);
#ifndef NVALGRIND
    if (ENABLE_REDZONES) {
        void* base = (char*)ptr - REDZONE_SIZE;
        global_heap.free(GCAllocation::fromUserData(base));
    } else {
        global_heap.free(GCAllocation::fromUserData(ptr));
    }
    VALGRIND_FREELIKE_BLOCK(ptr, REDZONE_SIZE);
#else
    global_heap.free(GCAllocation::fromUserData(ptr));
#endif
}

bool pyston::gc::MarkSweepGC::gcIsEnabled() {
    return gc_enabled;
}

void pyston::gc::MarkSweepGC::disableGC() {
    gc_enabled = false;
}

void pyston::gc::MarkSweepGC::enableGC() {
    gc_enabled = true;
}

void pyston::gc::MarkSweepGC::runCollection() {

}


void pyston::gc::MarkSweepGC::markPhase() {

}

void pyston::gc::MarkSweepGC::sweepPhase(std::vector<Box *> &weakly_referenced) {

}
