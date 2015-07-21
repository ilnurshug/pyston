//
// Created by user on 7/21/15.
//

#ifndef PYSTON_GC_BASE_H
#define PYSTON_GC_BASE_H


#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_set>
#include <vector>
#include <sys/mman.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "gc/heap.h"

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


        class TraceStack {
        private:
            const int CHUNK_SIZE = 256;
            const int MAX_FREE_CHUNKS = 50;

            std::vector<void**> chunks;
            static std::vector<void**> free_chunks;

            void** cur;
            void** start;
            void** end;

            void get_chunk();
            void release_chunk(void** chunk);
            void pop_chunk();

        public:
            TraceStack() { get_chunk(); }

            TraceStack(const std::unordered_set<void*>& rhs);

            void push(void* p);
            void* pop_chunk_and_item();
            void* pop();
        };
        //std::vector<void**> TraceStack::free_chunks;
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
}


#endif //PYSTON_GC_BASE_H
