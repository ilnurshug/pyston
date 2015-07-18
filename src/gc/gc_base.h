//
// Created by user on 7/18/15.
//

#ifndef PYSTON_GC_BASE_H
#define PYSTON_GC_BASE_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <sys/mman.h>

#include "core/common.h"
#include "core/threading.h"
#include "core/types.h"

namespace pyston {
    namespace gc {
        class GCBase;
    }

    class gc::GCBase {
    public:
        virtual void *gc_alloc(size_t bytes, GCKind kind_id) __attribute__((visibility("default"))) = 0;
        virtual void* gc_realloc(void* ptr, size_t bytes) __attribute__((visibility("default"))) = 0;
        virtual void gc_free(void* ptr) __attribute__((visibility("default"))) = 0;


        virtual void runCollection() = 0;

        virtual bool gcIsEnabled() = 0;
        virtual void disableGC() = 0;
        virtual void enableGC() = 0;

    protected:
        bool gc_enabled;
    };
}
#endif //PYSTON_GC_BASE_H
