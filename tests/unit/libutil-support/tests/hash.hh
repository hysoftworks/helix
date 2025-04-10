#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "lix/libutil/hash.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<Hash> {
    static Gen<Hash> arbitrary();
};

}
