//
// Created by user on 7/22/15.
//

#ifndef PYSTON_SEMISPACE_H
#define PYSTON_SEMISPACE_H

#include "gc/gc_base.h"

namespace pyston {
namespace gc {
    class SemiSpaceGC;
}
}

class pyston::gc::SemiSpaceGC : public GCBase {
public:
    SemiSpaceGC();

    virtual ~SemiSpaceGC() {}

    virtual void *gc_alloc(size_t bytes, GCKind kind_id) override;

    virtual void *gc_realloc(void *ptr, size_t bytes) override;

    virtual void gc_free(void *ptr) override;

    virtual void runCollection() override;
};


#endif //PYSTON_SEMISPACE_H
