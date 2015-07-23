//
// Created by user on 7/23/15.
//

#include "heap.h"
#include "core/common.h"
#include "core/util.h"
#include "gc/gc_alloc.h"
#include "runtime/types.h"

namespace pyston {
    namespace gc{
        unsigned bytesAllocatedSinceCollection;

        static StatCounter gc_registered_bytes("gc_registered_bytes");

        void _bytesAllocatedTripped() {
            gc_registered_bytes.log(bytesAllocatedSinceCollection);
            bytesAllocatedSinceCollection = 0;

            if (!gcIsEnabled())
                return;

            threading::GLPromoteRegion _lock;

            runCollection();
        }


        __attribute__((always_inline)) bool _doFree(GCAllocation* al, std::vector<Box*>* weakly_referenced) {
#ifndef NVALGRIND
    VALGRIND_DISABLE_ERROR_REPORTING;
#endif
            GCKind alloc_kind = al->kind_id;
#ifndef NVALGRIND
    VALGRIND_ENABLE_ERROR_REPORTING;
#endif

            if (alloc_kind == GCKind::PYTHON || alloc_kind == GCKind::CONSERVATIVE_PYTHON) {
#ifndef NVALGRIND
        VALGRIND_DISABLE_ERROR_REPORTING;
#endif
                Box* b = (Box*)al->user_data;
#ifndef NVALGRIND
        VALGRIND_ENABLE_ERROR_REPORTING;
#endif

                assert(b->cls);
                if (PyType_SUPPORTS_WEAKREFS(b->cls)) {
                    PyWeakReference** list = (PyWeakReference**)PyObject_GET_WEAKREFS_LISTPTR(b);
                    if (list && *list) {
                        assert(weakly_referenced && "attempting to free a weakly referenced object manually");
                        weakly_referenced->push_back(b);
                        return false;
                    }
                }

                // XXX: we are currently ignoring destructors (tp_dealloc) for extension objects, since we have
                // historically done that (whoops) and there are too many to be worth changing for now as long
                // as we can get real destructor support soon.
                        ASSERT(b->cls->tp_dealloc == NULL || alloc_kind == GCKind::CONSERVATIVE_PYTHON, "%s", getTypeName(b));

                if (b->cls->simple_destructor)
                    b->cls->simple_destructor(b);
            }
            return true;
        }
    }
}