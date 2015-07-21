// Copyright (c) 2014-2015 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gc/collector.h"
#include "gc/MarkSweepGC.h"

#include "runtime/types.h"

#ifndef NVALGRIND
#include "valgrind.h"
#endif

namespace pyston {
namespace gc {

    MarkSweepGC GC;


#if TRACE_GC_MARKING
FILE* trace_fp;
#endif



// Track the highest-addressed nonheap root; the assumption is that the nonheap roots will
// typically all have lower addresses than the heap roots, so this can serve as a cheap
// way to verify it's not a nonheap root (the full check requires a hashtable lookup).
static void* max_nonheap_root = 0;
static void* min_nonheap_root = (void*)~0;



static bool should_not_reenter_gc = false;


void registerPermanentRoot(void* obj, bool allow_duplicates) {
    assert(GC.global_heap.getAllocationFromInteriorPointer(obj));

    // Check for double-registers.  Wouldn't cause any problems, but we probably shouldn't be doing them.
    if (!allow_duplicates)
        ASSERT(GC.roots.count(obj) == 0, "Please only register roots once");

    GC.roots.insert(obj);
}

void deregisterPermanentRoot(void* obj) {
    assert(GC.global_heap.getAllocationFromInteriorPointer(obj));
    ASSERT(GC.roots.count(obj), "");
    GC.roots.erase(obj);
}

void registerPotentialRootRange(void* start, void* end) {
    GC.potential_root_ranges.push_back(std::make_pair(start, end));
}

extern "C" PyObject* PyGC_AddRoot(PyObject* obj) noexcept {
    if (obj) {
        // Allow duplicates from CAPI code since they shouldn't have to know
        // which objects we already registered as roots:
        registerPermanentRoot(obj, /* allow_duplicates */ true);
    }
    return obj;
}

void registerNonheapRootObject(void* obj, int size) {
    // I suppose that things could work fine even if this were true, but why would it happen?
    assert(GC.global_heap.getAllocationFromInteriorPointer(obj) == NULL);
    assert(GC.nonheap_roots.count(obj) == 0);

    GC.nonheap_roots.insert(obj);
    registerPotentialRootRange(obj, ((uint8_t*)obj) + size);

    max_nonheap_root = std::max(obj, max_nonheap_root);
    min_nonheap_root = std::min(obj, min_nonheap_root);
}

bool isNonheapRoot(void* p) {
    if (p > max_nonheap_root || p < min_nonheap_root)
        return false;
    return GC.nonheap_roots.count(p) != 0;
}

bool isValidGCMemory(void* p) {
    return isNonheapRoot(p) || (GC.global_heap.getAllocationFromInteriorPointer(p)->user_data == p);
}

bool isValidGCObject(void* p) {
    if (isNonheapRoot(p))
        return true;
    GCAllocation* al = GC.global_heap.getAllocationFromInteriorPointer(p);
    if (!al)
        return false;
    return al->user_data == p && (al->kind_id == GCKind::CONSERVATIVE_PYTHON || al->kind_id == GCKind::PYTHON);
}

void registerPythonObject(Box* b) {
    assert(isValidGCMemory(b));
    auto al = GCAllocation::fromUserData(b);

    if (al->kind_id == GCKind::CONSERVATIVE) {
        al->kind_id = GCKind::CONSERVATIVE_PYTHON;
    } else {
        assert(al->kind_id == GCKind::PYTHON);
    }

    assert(b->cls);
    if (PyType_Check(b)) {
        GC.class_objects.insert((BoxedClass*)b);
    }
}


bool gcIsEnabled() {
    return GC.gcIsEnabled();
}

void enableGC() {
    GC.enableGC();
}

void disableGC() {
    GC.disableGC();
}

void startGCUnexpectedRegion() {
    RELEASE_ASSERT(!should_not_reenter_gc, "");
    should_not_reenter_gc = true;
}

void endGCUnexpectedRegion() {
    RELEASE_ASSERT(should_not_reenter_gc, "");
    should_not_reenter_gc = false;
}

void runCollection() {
    GC.runCollection();
}

} // namespace gc
} // namespace pyston
