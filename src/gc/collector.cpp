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

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "codegen/ast_interpreter.h"
#include "codegen/codegen.h"
#include "core/common.h"
#include "core/threading.h"
#include "core/types.h"
#include "core/util.h"
#include "gc/heap.h"
#include "runtime/objmodel.h"
#include "runtime/types.h"



#ifndef NVALGRIND
#include "valgrind.h"
#endif

namespace pyston {
namespace gc {



#if TRACE_GC_MARKING
FILE* trace_fp;
#endif





// Track the highest-addressed nonheap root; the assumption is that the nonheap roots will
// typically all have lower addresses than the heap roots, so this can serve as a cheap
// way to verify it's not a nonheap root (the full check requires a hashtable lookup).
static void* max_nonheap_root = 0;
static void* min_nonheap_root = (void*)~0;



static bool should_not_reenter_gc = false;

class TraceStack {
private:
    const int CHUNK_SIZE = 256;
    const int MAX_FREE_CHUNKS = 50;

    std::vector<void**> chunks;
    static std::vector<void**> free_chunks;

    void** cur;
    void** start;
    void** end;

    void get_chunk() {
        if (free_chunks.size()) {
            start = free_chunks.back();
            free_chunks.pop_back();
        } else {
            start = (void**)malloc(sizeof(void*) * CHUNK_SIZE);
        }

        cur = start;
        end = start + CHUNK_SIZE;
    }
    void release_chunk(void** chunk) {
        if (free_chunks.size() == MAX_FREE_CHUNKS)
            free(chunk);
        else
            free_chunks.push_back(chunk);
    }
    void pop_chunk() {
        start = chunks.back();
        chunks.pop_back();
        end = start + CHUNK_SIZE;
        cur = end;
    }

public:
    TraceStack() { get_chunk(); }
    TraceStack(const std::unordered_set<void*>& rhs) {
        get_chunk();
        for (void* p : rhs) {
            assert(!isMarked(GCAllocation::fromUserData(p)));
            push(p);
        }
    }

    void push(void* p) {
        GC_TRACE_LOG("Pushing %p\n", p);
        GCAllocation* al = GCAllocation::fromUserData(p);
        if (isMarked(al))
            return;

        setMark(al);

        *cur++ = p;
        if (cur == end) {
            chunks.push_back(start);
            get_chunk();
        }
    }

    void* pop_chunk_and_item() {
        release_chunk(start);
        if (chunks.size()) {
            pop_chunk();
            assert(cur == end);
            return *--cur; // no need for any bounds checks here since we're guaranteed we're CHUNK_SIZE from the start
        } else {
            // We emptied the stack, but we should prepare a new chunk in case another item
            // gets added onto the stack.
            get_chunk();
            return NULL;
        }
    }


    void* pop() {
        if (cur > start)
            return *--cur;

        return pop_chunk_and_item();
    }
};
std::vector<void**> TraceStack::free_chunks;

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
    return isNonheapRoot(p) || (global_heap.getAllocationFromInteriorPointer(p)->user_data == p);
}

bool isValidGCObject(void* p) {
    if (isNonheapRoot(p))
        return true;
    GCAllocation* al = global_heap.getAllocationFromInteriorPointer(p);
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

GCRootHandle::GCRootHandle() {
    getRootHandles()->insert(this);
}
GCRootHandle::~GCRootHandle() {
    getRootHandles()->erase(this);
}

bool GCVisitor::isValid(void* p) {
    return GC.global_heap.getAllocationFromInteriorPointer(p) != NULL;
}

void GCVisitor::visit(void* p) {
    if ((uintptr_t)p < SMALL_ARENA_START || (uintptr_t)p >= HUGE_ARENA_START + ARENA_SIZE) {
        ASSERT(!p || isNonheapRoot(p), "%p", p);
        return;
    }

    ASSERT(GC.global_heap.getAllocationFromInteriorPointer(p)->user_data == p, "%p", p);
    stack->push(p);
}

void GCVisitor::visitRange(void* const* start, void* const* end) {
    ASSERT((const char*)end - (const char*)start <= 1000000000, "Asked to scan %.1fGB -- a bug?",
           ((const char*)end - (const char*)start) * 1.0 / (1 << 30));

    assert((uintptr_t)start % sizeof(void*) == 0);
    assert((uintptr_t)end % sizeof(void*) == 0);

    while (start < end) {
        visit(*start);
        start++;
    }
}

void GCVisitor::visitPotential(void* p) {
    GCAllocation* a = GC.global_heap.getAllocationFromInteriorPointer(p);
    if (a) {
        visit(a->user_data);
    }
}

void GCVisitor::visitPotentialRange(void* const* start, void* const* end) {
    ASSERT((const char*)end - (const char*)start <= 1000000000, "Asked to scan %.1fGB -- a bug?",
           ((const char*)end - (const char*)start) * 1.0 / (1 << 30));

    assert((uintptr_t)start % sizeof(void*) == 0);
    assert((uintptr_t)end % sizeof(void*) == 0);

    while (start < end) {
#if TRACE_GC_MARKING
        if (global_heap.getAllocationFromInteriorPointer(*start)) {
            if (*start >= (void*)HUGE_ARENA_START)
                GC_TRACE_LOG("Found conservative reference to huge object %p from %p\n", *start, start);
            else if (*start >= (void*)LARGE_ARENA_START && *start < (void*)HUGE_ARENA_START)
                GC_TRACE_LOG("Found conservative reference to large object %p from %p\n", *start, start);
            else
                GC_TRACE_LOG("Found conservative reference to %p from %p\n", *start, start);
        }
#endif

        visitPotential(*start);
        start++;
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
