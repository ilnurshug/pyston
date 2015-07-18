//
// Created by user on 7/18/15.
//

#ifndef PYSTON_MARKSWEEPGC_H
#define PYSTON_MARKSWEEPGC_H

#include "gc_base.h"
#include "gc/heap.h"
#include "runtime/objmodel.h"
#include "runtime/types.h"


#ifndef NVALGRIND
#include "valgrind.h"
#endif

namespace pyston{
    namespace gc{
        class MarkSweepGC;
    }

    class gc::MarkSweepGC : public gc::GCBase {
    public:
        MarkSweepGC() {
            gc_enabled = true;
        }
        virtual ~MarkSweepGC() {}

        virtual void *gc_alloc(size_t bytes, GCKind kind_id) override;

        virtual void *gc_realloc(void *ptr, size_t bytes) override;

        virtual void gc_free(void *ptr) override;

        virtual void runCollection() override;

        virtual bool gcIsEnabled() override;

        virtual void disableGC() override;

        virtual void enableGC() override;

    private:
        void markPhase();

        void sweepPhase(std::vector<Box*>& weakly_referenced);

        Heap global_heap;
    };
}



#endif //PYSTON_MARKSWEEPGC_H
