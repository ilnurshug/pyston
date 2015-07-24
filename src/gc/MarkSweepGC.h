//
// Created by user on 7/18/15.
//

#ifndef PYSTON_MARKSWEEPGC_H
#define PYSTON_MARKSWEEPGC_H

#include "gc/gc_base.h"


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
            global_heap = new DefaultHeap();
            gc_enabled = true;
            should_not_reenter_gc = false;
            ncollections = 0;
        }
        virtual ~MarkSweepGC() {
            delete global_heap;
        }

        virtual void runCollection() override;

    private:

        void markPhase();

        void sweepPhase(std::vector<Box*>& weakly_referenced);
    };
}



#endif //PYSTON_MARKSWEEPGC_H
