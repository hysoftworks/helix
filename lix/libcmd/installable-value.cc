#include "lix/libcmd/installable-value.hh"
#include "lix/libexpr/eval-cache.hh"
#include "lix/libfetchers/fetch-to-store.hh"
#include "lix/libutil/archive.hh"

namespace nix {

std::vector<ref<eval_cache::AttrCursor>>
InstallableValue::getCursors(EvalState & state)
{
    auto evalCache =
        std::make_shared<nix::eval_cache::EvalCache>(std::nullopt,
            [&](EvalState & state) { return toValue(state).first; });
    return {evalCache->getRoot()};
}

ref<eval_cache::AttrCursor>
InstallableValue::getCursor(EvalState & state)
{
    /* Although getCursors should return at least one element, in case it doesn't,
       bound check to avoid an undefined behavior for vector[0] */
    return getCursors(state).at(0);
}

static UsageError nonValueInstallable(Installable & installable)
{
    return UsageError("installable '%s' does not correspond to a Nix language value", installable.what());
}

InstallableValue & InstallableValue::require(Installable & installable)
{
    auto * castedInstallable = dynamic_cast<InstallableValue *>(&installable);
    if (!castedInstallable)
        throw nonValueInstallable(installable);
    return *castedInstallable;
}

ref<InstallableValue> InstallableValue::require(ref<Installable> installable)
{
    auto castedInstallable = installable.try_cast<InstallableValue>();
    if (!castedInstallable)
        throw nonValueInstallable(*installable);
    return *castedInstallable;
}

std::optional<DerivedPathWithInfo> InstallableValue::trySinglePathToDerivedPaths(
    EvalState & state, Value & v, const PosIdx pos, std::string_view errorCtx
)
{
    if (v.type() == nPath) {
        auto storePath = state.aio.blockOn(fetchToStoreRecursive(
            *evaluator->store,
            *prepareDump(state.ctx.paths.checkSourcePath(v.path()).canonical().abs())
        ));
        return {{
            .path = DerivedPath::Opaque {
                .path = std::move(storePath),
            },
            .info = make_ref<ExtraPathInfo>(),
        }};
    }

    else if (v.type() == nString) {
        return {{
            .path = DerivedPath::fromSingle(
                state.coerceToSingleDerivedPath(pos, v, errorCtx)),
            .info = make_ref<ExtraPathInfo>(),
        }};
    }

    else return std::nullopt;
}

}
