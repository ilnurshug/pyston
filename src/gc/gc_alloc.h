// Copyright (c) 2014-2015 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PYSTON_GC_GCALLOC_H
#define PYSTON_GC_GCALLOC_H

#include <cstdlib>

#include "gc/collector.h"
#include "gc/default_heap.h"

#include "gc/MarkSweepGC.h"

#ifndef NVALGRIND
#include "valgrind.h"
#endif

namespace pyston {

namespace gc {

    //extern MarkSweepGC GC;
    GCBase* GC();

#if STAT_ALLOCATIONS
static StatCounter gc_alloc_bytes("gc_alloc_bytes");
static StatCounter gc_alloc_bytes_typed[] = {
    StatCounter("gc_alloc_bytes_???"),          //
    StatCounter("gc_alloc_bytes_python"),       //
    StatCounter("gc_alloc_bytes_conservative"), //
    StatCounter("gc_alloc_bytes_precise"),      //
    StatCounter("gc_alloc_bytes_untracked"),    //
    StatCounter("gc_alloc_bytes_hidden_class"), //
};
#endif

#if STAT_TIMERS
extern uint64_t* gc_alloc_stattimer_counter;
#endif
extern "C" inline void* gc_alloc(size_t bytes, GCKind kind_id) {
    return GC()->gc_alloc(bytes, kind_id);
}

extern "C" inline void* gc_realloc(void* ptr, size_t bytes) {
    return GC()->gc_realloc(ptr, bytes);
}

extern "C" inline void gc_free(void* ptr) {
    GC()->gc_free(ptr);
}
}
}

#endif
