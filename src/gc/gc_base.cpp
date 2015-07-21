//
// Created by user on 7/21/15.
//

#include "gc_base.h"

#include "codegen/ast_interpreter.h"
#include "codegen/codegen.h"
#include "core/common.h"
#include "core/threading.h"
#include "core/types.h"
#include "core/util.h"
#include "runtime/objmodel.h"
#include "runtime/types.h"

namespace pyston {
    namespace gc {
        std::vector<void**> TraceStack::free_chunks;


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

        //--------------------TRACESTACK---------------------

        void TraceStack::get_chunk() {
            if (free_chunks.size()) {
                start = free_chunks.back();
                free_chunks.pop_back();
            } else {
                start = (void**)malloc(sizeof(void*) * CHUNK_SIZE);
            }

            cur = start;
            end = start + CHUNK_SIZE;
        }

        void TraceStack::release_chunk(void **chunk) {
            if (free_chunks.size() == MAX_FREE_CHUNKS)
                free(chunk);
            else
                free_chunks.push_back(chunk);
        }

        void TraceStack::pop_chunk() {
            start = chunks.back();
            chunks.pop_back();
            end = start + CHUNK_SIZE;
            cur = end;
        }

        void TraceStack::push(void *p) {
            GC_TRACE_LOG("Pushing %p\n", p);
            GCAllocation* al = GCAllocation::fromUserData(p);
            if (isMarked(al))
                return;

            setMark(al);

            *cur++ = p;
            if (cur == end) {
                chunks.push_back(start);
                get_chunk();
            }
        }

        void *TraceStack::pop_chunk_and_item() {
            release_chunk(start);
            if (chunks.size()) {
                pop_chunk();
                assert(cur == end);
                return *--cur; // no need for any bounds checks here since we're guaranteed we're CHUNK_SIZE from the start
            } else {
                // We emptied the stack, but we should prepare a new chunk in case another item
                // gets added onto the stack.
                get_chunk();
                return NULL;
            }
        }

        void *TraceStack::pop() {
            if (cur > start)
                return *--cur;

            return pop_chunk_and_item();
        }

        TraceStack::TraceStack(const std::unordered_set<void *> &rhs) {
            get_chunk();
            for (void* p : rhs) {
                assert(!isMarked(GCAllocation::fromUserData(p)));
                push(p);
            }
        }
    }
}

using namespace pyston;

std::unordered_set<BoxedClass*>         gc::GCBase::class_objects;
std::unordered_set<void*>               gc::GCBase::roots;
std::vector<std::pair<void*, void*>>    gc::GCBase::potential_root_ranges;
std::unordered_set<void*>               gc::GCBase::nonheap_roots;