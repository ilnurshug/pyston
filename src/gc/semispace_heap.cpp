//
// Created by user on 7/23/15.
//

#include "semispace_heap.h"

pyston::gc::SemiSpaceHeap::SemiSpaceHeap() {
    fromspace = new Arena<SMALL_ARENA_START, ARENA_SIZE, initial_map_size, increment>();
    tospace   = new Arena<LARGE_ARENA_START, ARENA_SIZE, initial_map_size, increment>();
}

pyston::gc::SemiSpaceHeap::~SemiSpaceHeap() {
    delete fromspace;
    delete tospace;
}

pyston::gc::GCAllocation *pyston::gc::SemiSpaceHeap::alloc(size_t bytes) {
    registerGCManagedBytes(bytes);

    Obj* obj = _alloc(bytes + sizeof(GCAllocation) + sizeof(Obj));
    obj->size = bytes;

    return obj->data;
}

pyston::gc::SemiSpaceHeap::Obj* pyston::gc::SemiSpaceHeap::_alloc(size_t size) {
    void* old_frontier = tospace->frontier;

    void* obj = tospace->allocFromArena(size);

// TODO: сделать extend во время GC

    if ((uint8_t*)old_frontier < (uint8_t*)tospace->frontier) {
        size_t grow_size = (size + increment - 1) & ~(increment - 1);
        fromspace->extendMapping(grow_size);
    }

return (Obj*)obj;
}

pyston::gc::GCAllocation *pyston::gc::SemiSpaceHeap::realloc(pyston::gc::GCAllocation *al, size_t bytes) {
    Obj* obj = Obj::fromAllocation(al);
    size_t size = obj->size;
    if (size >= bytes && size < bytes * 2)
        return al;

    GCAllocation* rtn = alloc(bytes);
    memcpy(rtn, al, std::min(bytes, size));

    destructContents(al);

    return rtn;
}

void pyston::gc::SemiSpaceHeap::destructContents(pyston::gc::GCAllocation *alloc) {
    _doFree(alloc, NULL);
}

void pyston::gc::SemiSpaceHeap::free(pyston::gc::GCAllocation *alloc) {
    destructContents(alloc);
}

pyston::gc::GCAllocation *pyston::gc::SemiSpaceHeap::getAllocationFromInteriorPointer(void *ptr) {
    return nullptr;
}

void pyston::gc::SemiSpaceHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {

}

void pyston::gc::SemiSpaceHeap::prepareForCollection() {

}

void pyston::gc::SemiSpaceHeap::cleanupAfterCollection() {

}

void pyston::gc::SemiSpaceHeap::dumpHeapStatistics(int level) {

}
