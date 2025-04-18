#pragma once
///@file

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <rapidcheck/gen/Arbitrary.h>
#pragma GCC diagnostic pop

#include "lix/libstore/path.hh"

namespace nix {

struct StorePathName {
    std::string name;
};

// For rapidcheck
void showValue(const StorePath & p, std::ostream & os);

}

namespace rc {
using namespace nix;

template<>
struct Arbitrary<StorePathName> {
    static Gen<StorePathName> arbitrary();
};

template<>
struct Arbitrary<StorePath> {
    static Gen<StorePath> arbitrary();
};

}
