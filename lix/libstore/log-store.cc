#include "lix/libstore/log-store.hh"

namespace nix {

kj::Promise<Result<std::optional<std::string>>> LogStore::getBuildLog(const StorePath & path)
try {
    auto maybePath = getBuildDerivationPath(path);
    if (!maybePath)
        co_return std::nullopt;
    co_return getBuildLogExact(maybePath.value());
} catch (...) {
    co_return result::current_exception();
}

}
