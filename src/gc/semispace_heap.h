//
// Created by user on 7/23/15.
//

#ifndef PYSTON_SEMISPACE_HEAP_H
#define PYSTON_SEMISPACE_HEAP_H

#include "heap.h"

namespace pyston {
namespace gc {

    class SemiSpaceHeap : public Heap {
    private:
        static const uintptr_t increment = 16 * 1024 * 1024;
        static const uintptr_t initial_map_size = 64 * 1024 * 1024;

        Arena<SMALL_ARENA_START, ARENA_SIZE, initial_map_size, increment>* fromspace;
        Arena<LARGE_ARENA_START, ARENA_SIZE, initial_map_size, increment>* tospace;

        struct Obj {
            size_t size;
            Obj* forward;
            GCAllocation data[0];

            Obj() { forward = nullptr; }

            static Obj* fromAllocation(GCAllocation* alloc) {
                char* rtn = (char*)alloc - offsetof(Obj, data);
                return reinterpret_cast<Obj*>(rtn);
            }
        };

    public:
        SemiSpaceHeap();

        virtual ~SemiSpaceHeap();

        virtual GCAllocation *alloc(size_t bytes) override;

        virtual GCAllocation *realloc(GCAllocation *alloc, size_t bytes) override;

        virtual void destructContents(GCAllocation *alloc) override;

        virtual void free(GCAllocation *alloc) override;

        virtual GCAllocation *getAllocationFromInteriorPointer(void *ptr) override;

        virtual void freeUnmarked(std::vector<Box *> &weakly_referenced) override;

        virtual void prepareForCollection() override;

        virtual void cleanupAfterCollection() override;

        virtual void dumpHeapStatistics(int level) override;

    private:

        Obj* _alloc(size_t size);
    };

}
}


#endif //PYSTON_SEMISPACE_HEAP_H
