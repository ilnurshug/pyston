//
// Created by user on 7/22/15.
//

#include "semispace.h"
#include "gc/semispace_heap.h"

namespace pyston {
namespace gc{

    SemiSpaceGC::SemiSpaceGC() {
        global_heap = new SemiSpaceHeap();
        gc_enabled = true;
        should_not_reenter_gc = false;
        ncollections = 0;
    }

    void SemiSpaceGC::runCollection() {
        global_heap->prepareForCollection();

        RELEASE_ASSERT(!should_not_reenter_gc, "");
        should_not_reenter_gc = true; // begin non-reentrant section

        // action goes here
        flip();

        should_not_reenter_gc = false; // end non-reentrant section

        global_heap->cleanupAfterCollection();
    }

    void SemiSpaceGC::flip() {
        auto heap = (SemiSpaceHeap*)global_heap;

        std::swap(heap->tospace, heap->fromspace);



    }

} // namespace gc
} // namespace pyston


