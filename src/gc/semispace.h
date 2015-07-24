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

    virtual void runCollection() override;

private:

    void flip();

};


#endif //PYSTON_SEMISPACE_H
