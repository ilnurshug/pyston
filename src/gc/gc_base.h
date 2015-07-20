//
// Created by user on 7/18/15.
//

#ifndef PYSTON_GC_BASE_H
#define PYSTON_GC_BASE_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <sys/mman.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "codegen/ast_interpreter.h"
#include "codegen/codegen.h"
#include "core/common.h"
#include "core/threading.h"
#include "core/types.h"
#include "core/util.h"
#include "gc/heap.h"
#include "runtime/objmodel.h"
#include "runtime/types.h"

namespace pyston {
    namespace gc {
        #define TRACE_GC_MARKING 0
        #if TRACE_GC_MARKING
        extern FILE* trace_fp;
        #define GC_TRACE_LOG(...) fprintf(pyston::gc::trace_fp, __VA_ARGS__)
        #else
        #define GC_TRACE_LOG(...)
        #endif


        // If you want to have a static root "location" where multiple values could be stored, use this:
        class GCRootHandle;

        class GCBase;

        static std::unordered_set<GCRootHandle*>* getRootHandles() {
            static std::unordered_set<GCRootHandle*> root_handles;
            return &root_handles;
        }
    }

    class gc::GCRootHandle  {
    public:
        Box* value;

        GCRootHandle() { gc::getRootHandles()->insert(this); }
        ~GCRootHandle() { gc :: getRootHandles()->erase(this); };

        void operator=(Box* b) { value = b; }

        operator Box*() { return value; }
        Box* operator->() { return value; }
    };

    class gc::GCBase {
    public:
        virtual ~GCBase() {}
        virtual void *gc_alloc(size_t bytes, GCKind kind_id) __attribute__((visibility("default"))) = 0;
        virtual void* gc_realloc(void* ptr, size_t bytes) __attribute__((visibility("default"))) = 0;
        virtual void gc_free(void* ptr) __attribute__((visibility("default"))) = 0;


        virtual void runCollection() = 0;

        virtual bool gcIsEnabled() = 0;
        virtual void disableGC() = 0;
        virtual void enableGC() = 0;

        Heap global_heap;

        static std::unordered_set<BoxedClass*> class_objects;

        static std::unordered_set<void*> roots;
        static std::vector<std::pair<void*, void*>> potential_root_ranges;
        static std::unordered_set<void*> nonheap_roots;

    protected:
        bool gc_enabled;

        __attribute__((always_inline)) void visitByGCKind(void* p, GCVisitor& visitor);

        void graphTraversalMarking(TraceStack& stack, GCVisitor& visitor);

        void markRoots(GCVisitor& visitor);
    };

    void gc::GCBase::visitByGCKind(void *p, GCVisitor &visitor) {
        assert(((intptr_t)p) % 8 == 0);

        GCAllocation* al = GCAllocation::fromUserData(p);

        GCKind kind_id = al->kind_id;
        if (kind_id == GCKind::UNTRACKED) {
            // Nothing to do here.
        } else if (kind_id == GCKind::CONSERVATIVE || kind_id == GCKind::CONSERVATIVE_PYTHON) {
            uint32_t bytes = al->kind_data;
            visitor.visitPotentialRange((void**)p, (void**)((char*)p + bytes));
        } else if (kind_id == GCKind::PRECISE) {
            uint32_t bytes = al->kind_data;
            visitor.visitRange((void**)p, (void**)((char*)p + bytes));
        } else if (kind_id == GCKind::PYTHON) {
            Box* b = reinterpret_cast<Box*>(p);
            BoxedClass* cls = b->cls;

            if (cls) {
                // The cls can be NULL since we use 'new' to construct them.
                // An arbitrary amount of stuff can happen between the 'new' and
                // the call to the constructor (ie the args get evaluated), which
                // can trigger a collection.
                ASSERT(cls->gc_visit, "%s", getTypeName(b));
                cls->gc_visit(&visitor, b);
            }
        } else if (kind_id == GCKind::HIDDEN_CLASS) {
            HiddenClass* hcls = reinterpret_cast<HiddenClass*>(p);
            hcls->gc_visit(&visitor);
        } else {
            RELEASE_ASSERT(0, "Unhandled kind: %d", (int)kind_id);
        }
    }

    void gc::GCBase::graphTraversalMarking(gc::TraceStack &stack, GCVisitor &visitor) {
        static StatCounter sc_us("us_gc_mark_phase_graph_traversal");
        static StatCounter sc_marked_objs("gc_marked_object_count");
        Timer _t("traversing", /*min_usec=*/10000);

        while (void* p = stack.pop()) {
            sc_marked_objs.log();

            GCAllocation* al = GCAllocation::fromUserData(p);

#if TRACE_GC_MARKING
        if (al->kind_id == GCKind::PYTHON || al->kind_id == GCKind::CONSERVATIVE_PYTHON)
            GC_TRACE_LOG("Looking at %s object %p\n", static_cast<Box*>(p)->cls->tp_name, p);
        else
            GC_TRACE_LOG("Looking at non-python allocation %p\n", p);
#endif

            assert(isMarked(al));
            visitByGCKind(p, visitor);
        }

        long us = _t.end();
        sc_us.log(us);
    }

    void gc::GCBase::markRoots(GCVisitor &visitor) {
        GC_TRACE_LOG("Looking at the stack\n");
        threading::visitAllStacks(&visitor);

        GC_TRACE_LOG("Looking at root handles\n");
        for (auto h : *getRootHandles()) {
            visitor.visit(h->value);
        }

        GC_TRACE_LOG("Looking at potential root ranges\n");
        for (auto& e : potential_root_ranges) {
            visitor.visitPotentialRange((void* const*)e.first, (void* const*)e.second);
        }
    }
}
#endif //PYSTON_GC_BASE_H
