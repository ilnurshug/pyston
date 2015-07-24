

#ifndef PYSTON_GC_HEAP_H
#define PYSTON_GC_HEAP_H

#include <cstddef>

#include "core/common.h"
#include "core/threading.h"
#include "core/types.h"

#include <sys/mman.h>

namespace pyston {
namespace gc {

    typedef uint8_t kindid_t;
    struct GCAllocation {
        unsigned int gc_flags : 8;
        GCKind kind_id : 8;
        unsigned int _reserved1 : 16;
        unsigned int kind_data : 32;

        char user_data[0];

        static GCAllocation* fromUserData(void* user_data) {
            char* d = reinterpret_cast<char*>(user_data);
            return reinterpret_cast<GCAllocation*>(d - offsetof(GCAllocation, user_data));
        }
    };
    static_assert(sizeof(GCAllocation) <= sizeof(void*),
                  "we should try to make sure the gc header is word-sized or smaller");
    /*
        Heap Interface
    */
    class Heap {
    public:
        DS_DEFINE_SPINLOCK(lock);

        virtual ~Heap() {}

        virtual GCAllocation* __attribute__((__malloc__)) alloc(size_t bytes) = 0;

        virtual GCAllocation* realloc(GCAllocation* alloc, size_t bytes) = 0;

        virtual void destructContents(GCAllocation* alloc) = 0;

        virtual void free(GCAllocation* alloc) = 0;

        virtual GCAllocation* getAllocationFromInteriorPointer(void* ptr) = 0;

        virtual void freeUnmarked(std::vector<Box*>& weakly_referenced) = 0;

        virtual void prepareForCollection() = 0;

        virtual void cleanupAfterCollection() = 0;

        virtual void dumpHeapStatistics(int level) = 0;
    };


    extern unsigned bytesAllocatedSinceCollection;
#define ALLOCBYTES_PER_COLLECTION 10000000
        void _bytesAllocatedTripped();

// Notify the gc of n bytes as being under GC management.
// This is called internally for anything allocated through gc_alloc,
// but it can also be called by clients to say that they have memory that
// is ultimately GC managed but did not get allocated through gc_alloc,
// such as memory that will get freed by a gc destructor.
        inline void registerGCManagedBytes(size_t bytes) {
            bytesAllocatedSinceCollection += bytes;
            if (unlikely(bytesAllocatedSinceCollection >= ALLOCBYTES_PER_COLLECTION)) {
                _bytesAllocatedTripped();
            }
        }

#define PAGE_SIZE 4096

        template <uintptr_t arena_size, uintptr_t initial_mapsize, uintptr_t increment> class Arena {
        public:
            uintptr_t arena_start;

            void* cur;
            void* frontier;
            void* arena_end;

            Arena(uintptr_t arena_start) : arena_start(arena_start), cur((void*)arena_start), frontier((void*)arena_start), arena_end((void*)(arena_start + arena_size)) {
                if (initial_mapsize)
                    extendMapping(initial_mapsize);
            }

            // extends the mapping for this arena
            void extendMapping(size_t size) {
                assert(size % PAGE_SIZE == 0);

                assert(((uint8_t*)frontier + size) < arena_end && "arena full");

                void* mrtn = mmap(frontier, size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                RELEASE_ASSERT((uintptr_t)mrtn != -1, "failed to allocate memory from OS");
                        ASSERT(mrtn == frontier, "%p %p\n", mrtn, cur);

                frontier = (uint8_t*)frontier + size;
            }

            void* allocFromArena(size_t size) {
                if (((char*)cur + size) >= (char*)frontier) {
                    // grow the arena by a multiple of increment such that we can service the allocation request
                    size_t grow_size = (size + increment - 1) & ~(increment - 1);
                    extendMapping(grow_size);
                }

                void* rtn = cur;
                cur = (uint8_t*)cur + size;
                return rtn;
            }

            bool contains(void* addr) { return (void*)arena_start <= addr && addr < cur; }
        };

        constexpr uintptr_t ARENA_SIZE = 0x1000000000L;
        constexpr uintptr_t SMALL_ARENA_START = 0x1270000000L;
        constexpr uintptr_t LARGE_ARENA_START = 0x2270000000L;
        constexpr uintptr_t HUGE_ARENA_START = 0x3270000000L;

        bool _doFree(GCAllocation* al, std::vector<Box*>* weakly_referenced);
}
}

#endif

