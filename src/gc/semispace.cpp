//
// Created by user on 7/22/15.
//

#include "semispace.h"
#include "gc/semispace_heap.h"

pyston::gc::SemiSpaceGC::SemiSpaceGC() {
    global_heap = new SemiSpaceHeap();
}

void *pyston::gc::SemiSpaceGC::gc_alloc(size_t bytes, pyston::gc::GCKind kind_id) {
    return nullptr;
}

void *pyston::gc::SemiSpaceGC::gc_realloc(void *ptr, size_t bytes) {
    return nullptr;
}

void pyston::gc::SemiSpaceGC::gc_free(void *ptr) {

}

void pyston::gc::SemiSpaceGC::runCollection() {

}
