//
// Created by user on 7/18/15.
//

#include "MarkSweepGC.h"

#include "runtime/objmodel.h"
#include "runtime/types.h"

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
    FILE *f = fopen("out.txt", "a");
    fprintf(f, "hello from runCollection (Mark-and-Sweep GC class)\n");

    static StatCounter sc_us("us_gc_collections");
    static StatCounter sc("gc_collections");
    sc.log();

    UNAVOIDABLE_STAT_TIMER(t0, "us_timer_gc_collection");

    ncollections++;

    if (VERBOSITY("gc") >= 2)
        printf("Collection #%d\n", ncollections);

    // The bulk of the GC work is not reentrant-safe.
    // In theory we should never try to reenter that section, but it's happened due to bugs,
    // which show up as very-hard-to-understand gc issues.
    // So keep track if we're in the non-reentrant section and abort if we try to go back in.
    // We could also just skip the collection if we're currently in the gc, but I think if we
    // run into this case it's way more likely that it's a bug than something we should ignore.
    RELEASE_ASSERT(!should_not_reenter_gc, "");
    should_not_reenter_gc = true; // begin non-reentrant section

    Timer _t("collecting", /*min_usec=*/10000);

    global_heap.prepareForCollection();

    markPhase();

    // The sweep phase will not free weakly-referenced objects, so that we can inspect their
    // weakrefs_list.  We want to defer looking at those lists until the end of the sweep phase,
    // since the deallocation of other objects (namely, the weakref objects themselves) can affect
    // those lists, and we want to see the final versions.
    std::vector<Box*> weakly_referenced;
    sweepPhase(weakly_referenced);

    // Handle weakrefs in two passes:
    // - first, find all of the weakref objects whose callbacks we need to call.  we need to iterate
    //   over the garbage-and-corrupt-but-still-alive weakly_referenced list in order to find these objects,
    //   so the gc is not reentrant during this section.  after this we discard that list.
    // - then, call all the weakref callbacks we collected from the first pass.

    // Use a StlCompatAllocator to keep the pending weakref objects alive in case we trigger a new collection.
    // In theory we could push so much onto this list that we would cause a new collection to start:
    std::list<PyWeakReference*, pyston::StlCompatAllocator<PyWeakReference*>> weak_references;

    for (auto o : weakly_referenced) {
        assert(isValidGCObject(o));
        PyWeakReference** list = (PyWeakReference**)PyObject_GET_WEAKREFS_LISTPTR(o);
        while (PyWeakReference* head = *list) {
            assert(isValidGCObject(head));
            if (head->wr_object != Py_None) {
                assert(head->wr_object == o);
                _PyWeakref_ClearRef(head);

                if (head->wr_callback)
                    weak_references.push_back(head);
            }
        }
        global_heap.free(GCAllocation::fromUserData(o));
    }

    should_not_reenter_gc = false; // end non-reentrant section

    while (!weak_references.empty()) {
        PyWeakReference* head = weak_references.front();
        weak_references.pop_front();

        if (head->wr_callback) {

            runtimeCall(head->wr_callback, ArgPassSpec(1), reinterpret_cast<Box*>(head), NULL, NULL, NULL, NULL);
            head->wr_callback = NULL;
        }
    }

    global_heap.cleanupAfterCollection();

    if (VERBOSITY("gc") >= 2)
        printf("Collection #%d done\n\n", ncollections);

    long us = _t.end();
    sc_us.log(us);

    // dumpHeapStatistics();
}


void pyston::gc::MarkSweepGC::markPhase() {
    static StatCounter sc_us("us_gc_mark_phase");
    Timer _t("markPhase", /*min_usec=*/10000);

#ifndef NVALGRIND
    // Have valgrind close its eyes while we do the conservative stack and data scanning,
    // since we'll be looking at potentially-uninitialized values:
    VALGRIND_DISABLE_ERROR_REPORTING;
#endif

#if TRACE_GC_MARKING
#if 1 // separate log file per collection
    char tracefn_buf[80];
    snprintf(tracefn_buf, sizeof(tracefn_buf), "gc_trace_%03d.txt", ncollections);
    trace_fp = fopen(tracefn_buf, "w");
#else // overwrite previous log file with each collection
    trace_fp = fopen("gc_trace.txt", "w");
#endif
#endif
    GC_TRACE_LOG("Starting collection %d\n", ncollections);

    GC_TRACE_LOG("Looking at roots\n");
    TraceStack stack(roots);
    GCVisitor visitor(&stack);

    markRoots(visitor);

    graphTraversalMarking(stack, visitor);

    // Some classes might be unreachable. Unfortunately, we have to keep them around for
    // one more collection, because during the sweep phase, instances of unreachable
    // classes might still end up looking at the class. So we visit those unreachable
    // classes remove them from the list of class objects so that it can be freed
    // in the next collection.
    std::vector<BoxedClass*> classes_to_remove;
    for (BoxedClass* cls : class_objects) {
        GCAllocation* al = GCAllocation::fromUserData(cls);
        if (!isMarked(al)) {
            visitor.visit(cls);
            classes_to_remove.push_back(cls);
        }
    }

    // We added new objects to the stack again from visiting classes so we nee to do
    // another (mini) traversal.
    graphTraversalMarking(stack, visitor);

    for (BoxedClass* cls : classes_to_remove) {
        class_objects.erase(cls);
    }

    // The above algorithm could fail if we have a class and a metaclass -- they might
    // both have been added to the classes to remove. In case that happens, make sure
    // that the metaclass is retained for at least another collection.
    for (BoxedClass* cls : classes_to_remove) {
        class_objects.insert(cls->cls);
    }

#if TRACE_GC_MARKING
    fclose(trace_fp);
    trace_fp = NULL;
#endif

#ifndef NVALGRIND
    VALGRIND_ENABLE_ERROR_REPORTING;
#endif

    long us = _t.end();
    sc_us.log(us);
}

void pyston::gc::MarkSweepGC::sweepPhase(std::vector<Box *> &weakly_referenced) {
    static StatCounter sc_us("us_gc_sweep_phase");
    Timer _t("sweepPhase", /*min_usec=*/10000);

    // we need to use the allocator here because these objects are referenced only here, and calling the weakref
    // callbacks could start another gc
    global_heap.freeUnmarked(weakly_referenced);

    long us = _t.end();
    sc_us.log(us);
}




