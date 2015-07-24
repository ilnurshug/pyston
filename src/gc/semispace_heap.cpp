//
// Created by user on 7/23/15.
//

#include "semispace_heap.h"

namespace pyston {
namespace gc {
    SemiSpaceHeap::SemiSpaceHeap() {
        fromspace = new Arena<ARENA_SIZE, initial_map_size, increment>(SMALL_ARENA_START);
        tospace   = new Arena<ARENA_SIZE, initial_map_size, increment>(LARGE_ARENA_START);
    }

    SemiSpaceHeap::~SemiSpaceHeap() {
        delete fromspace;
        delete tospace;
    }

    GCAllocation *SemiSpaceHeap::alloc(size_t bytes) {
        registerGCManagedBytes(bytes);

        Obj* obj = _alloc(bytes + sizeof(GCAllocation) + sizeof(Obj));
        obj->size = bytes;

        return obj->data;
    }

    SemiSpaceHeap::Obj* SemiSpaceHeap::_alloc(size_t size) {
        void* old_frontier = tospace->frontier;

        void* obj = tospace->allocFromArena(size);

// TODO: сделать extend во время GC

        if ((uint8_t*)old_frontier < (uint8_t*)tospace->frontier) {
            size_t grow_size = (size + increment - 1) & ~(increment - 1);
            fromspace->extendMapping(grow_size);
        }

        return (Obj*)obj;
    }

    GCAllocation *SemiSpaceHeap::realloc(GCAllocation *al, size_t bytes) {
        Obj* obj = Obj::fromAllocation(al);
        size_t size = obj->size;
        if (size >= bytes && size < bytes * 2)
            return al;

        GCAllocation* rtn = alloc(bytes);
        memcpy(rtn, al, std::min(bytes, size));

        destructContents(al);

        return rtn;
    }

    void SemiSpaceHeap::destructContents(GCAllocation *alloc) {
        _doFree(alloc, NULL);
    }

    void SemiSpaceHeap::free(GCAllocation *alloc) {
        destructContents(alloc);
    }

    GCAllocation *SemiSpaceHeap::getAllocationFromInteriorPointer(void *ptr) {
        return nullptr;
    }

    void SemiSpaceHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {

    }

    void SemiSpaceHeap::prepareForCollection() {

    }

    void SemiSpaceHeap::cleanupAfterCollection() {

    }

    void SemiSpaceHeap::dumpHeapStatistics(int level) {

    }
} // namespace gc
} // namespace pyston
