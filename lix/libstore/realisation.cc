#include "lix/libstore/realisation.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/closure.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/result.hh"

namespace nix {

MakeError(InvalidDerivationOutputId, Error);

DrvOutput DrvOutput::parse(const std::string &strRep) {
    size_t n = strRep.find("!");
    if (n == strRep.npos)
        throw InvalidDerivationOutputId("Invalid derivation output id %s", strRep);

    return DrvOutput{
        .drvHash = Hash::parseAnyPrefixed(strRep.substr(0, n)),
        .outputName = strRep.substr(n+1),
    };
}

std::string DrvOutput::to_string() const {
    return strHash() + "!" + outputName;
}

kj::Promise<Result<std::set<Realisation>>>
Realisation::closure(Store & store, const std::set<Realisation> & startOutputs)
try {
    std::set<Realisation> res;
    TRY_AWAIT(Realisation::closure(store, startOutputs, res));
    co_return res;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> Realisation::closure(
    Store & store, const std::set<Realisation> & startOutputs, std::set<Realisation> & res
)
try {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    auto getDeps = [&](const Realisation& current) -> kj::Promise<Result<std::set<Realisation>>> {
        try {
            std::set<Realisation> res;
            for (auto& [currentDep, _] : current.dependentRealisations) {
                if (auto currentRealisation = TRY_AWAIT(store.queryRealisation(currentDep)))
                    res.insert(*currentRealisation);
                else
                    throw Error(
                        "Unrealised derivation '%s'", currentDep.to_string());
            }
            co_return res;
        } catch (...) {
            co_return result::current_exception();
        }
    };

    res.merge(TRY_AWAIT(computeClosureAsync<Realisation>(startOutputs, getDeps)));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

JSON Realisation::toJSON() const {
    auto jsonDependentRealisations = JSON::object();
    for (auto & [depId, depOutPath] : dependentRealisations)
        jsonDependentRealisations.emplace(depId.to_string(), depOutPath.to_string());
    return JSON{
        {"id", id.to_string()},
        {"outPath", outPath.to_string()},
        {"signatures", signatures},
        {"dependentRealisations", jsonDependentRealisations},
    };
}

Realisation Realisation::fromJSON(
    const JSON& json,
    const std::string& whence) {
    auto getOptionalField = [&](std::string fieldName) -> std::optional<std::string> {
        auto fieldIterator = json.find(fieldName);
        if (fieldIterator == json.end())
            return std::nullopt;
        return {*fieldIterator};
    };
    auto getField = [&](std::string fieldName) -> std::string {
        if (auto field = getOptionalField(fieldName))
            return *field;
        else
            throw Error(
                "Drv output info file '%1%' is corrupt, missing field %2%",
                whence, fieldName);
    };

    StringSet signatures;
    if (auto signaturesIterator = json.find("signatures"); signaturesIterator != json.end())
        signatures.insert(signaturesIterator->begin(), signaturesIterator->end());

    std::map <DrvOutput, StorePath> dependentRealisations;
    if (auto jsonDependencies = json.find("dependentRealisations"); jsonDependencies != json.end())
        for (auto & [jsonDepId, jsonDepOutPath] : jsonDependencies->get<std::map<std::string, std::string>>())
            dependentRealisations.insert({DrvOutput::parse(jsonDepId), StorePath(jsonDepOutPath)});

    return Realisation{
        .id = DrvOutput::parse(getField("id")),
        .outPath = StorePath(getField("outPath")),
        .signatures = signatures,
        .dependentRealisations = dependentRealisations,
    };
}

std::string Realisation::fingerprint() const
{
    auto serialized = toJSON();
    serialized.erase("signatures");
    return serialized.dump();
}

void Realisation::sign(const SecretKey & secretKey)
{
    signatures.insert(secretKey.signDetached(fingerprint()));
}

bool Realisation::checkSignature(const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(), sig, publicKeys);
}

size_t Realisation::checkSignatures(const PublicKeys & publicKeys) const
{
    // FIXME: Maybe we should return `maxSigs` if the realisation corresponds to
    // an input-addressed one − because in that case the drv is enough to check
    // it − but we can't know that here.

    size_t good = 0;
    for (auto & sig : signatures)
        if (checkSignature(publicKeys, sig))
            good++;
    return good;
}


SingleDrvOutputs filterDrvOutputs(const OutputsSpec& wanted, SingleDrvOutputs&& outputs)
{
    SingleDrvOutputs ret = std::move(outputs);
    for (auto it = ret.begin(); it != ret.end(); ) {
        if (!wanted.contains(it->first))
            it = ret.erase(it);
        else
            ++it;
    }
    return ret;
}

StorePath RealisedPath::path() const {
    return std::visit([](auto && arg) { return arg.getPath(); }, raw);
}

bool Realisation::isCompatibleWith(const Realisation & other) const
{
    assert (id == other.id);
    if (outPath == other.outPath) {
        if (dependentRealisations.empty() != other.dependentRealisations.empty()) {
            warn(
                "Encountered a realisation for '%s' with an empty set of "
                "dependencies. This is likely an artifact from an older Nix. "
                "I’ll try to fix the realisation if I can",
                id.to_string());
            return true;
        } else if (dependentRealisations == other.dependentRealisations) {
            return true;
        }
    }
    return false;
}

kj::Promise<Result<void>> RealisedPath::closure(
    Store& store,
    const RealisedPath::Set& startPaths,
    RealisedPath::Set& ret)
try {
    // FIXME: This only builds the store-path closure, not the real realisation
    // closure
    StorePathSet initialStorePaths, pathsClosure;
    for (auto& path : startPaths)
        initialStorePaths.insert(path.path());
    TRY_AWAIT(store.computeFSClosure(initialStorePaths, pathsClosure));
    ret.insert(startPaths.begin(), startPaths.end());
    ret.insert(pathsClosure.begin(), pathsClosure.end());
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RealisedPath::closure(Store& store, RealisedPath::Set & ret) const
try {
    TRY_AWAIT(RealisedPath::closure(store, {*this}, ret));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<RealisedPath::Set>> RealisedPath::closure(Store& store) const
try {
    RealisedPath::Set ret;
    TRY_AWAIT(closure(store, ret));
    co_return ret;
} catch (...) {
    co_return result::current_exception();
}

} // namespace nix
