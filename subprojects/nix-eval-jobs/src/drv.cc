#include <lix/config.h> // IWYU pragma: keep

#include <lix/libstore/path-with-outputs.hh>
#include <lix/libstore/store-api.hh>
#include <lix/libstore/local-fs-store.hh>
#include <lix/libexpr/value-to-json.hh>
#include <lix/libstore/derivations.hh>
#include <stdint.h>
#include <lix/libstore/derived-path-map.hh>
#include <lix/libexpr/eval.hh>
#include <lix/libexpr/get-drvs.hh>
#include <lix/libexpr/nixexpr.hh>
#include <lix/libstore/path.hh>
#include <lix/libutil/json.hh>
#include <lix/libutil/ref.hh>
#include <lix/libexpr/value/context.hh>
#include <exception>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "drv.hh"
#include "eval-args.hh"
#include "lix/libutil/async.hh"

static bool
queryIsCached(nix::AsyncIoRoot &aio,
              nix::Store &store,
              std::map<std::string, std::optional<std::string>> &outputs) {
    uint64_t downloadSize, narSize;
    nix::StorePathSet willBuild, willSubstitute, unknown;

    std::vector<nix::StorePathWithOutputs> paths;
    for (auto const &[key, val] : outputs) {
        if (val) {
            paths.push_back(followLinksToStorePathWithOutputs(store, *val));
        }
    }

    aio.blockOn(store.queryMissing(toDerivedPaths(paths), willBuild, willSubstitute,
                       unknown, downloadSize, narSize));
    return willBuild.empty() && unknown.empty();
}

/* The fields of a derivation that are printed in json form */
Drv::Drv(std::string &attrPath, nix::EvalState &state, nix::DrvInfo &drvInfo,
         MyArgs &args, std::optional<Constituents> constituents)
    : constituents(constituents) {

    auto localStore = state.ctx.store.dynamic_pointer_cast<nix::LocalFSStore>();

    try {
        // CA derivations do not have static output paths, so we have to
        // defensively not query output paths in case we encounter one.
        for (auto &[outputName, optOutputPath] :
             drvInfo.queryOutputs(state, !nix::experimentalFeatureSettings.isEnabled(
                 nix::Xp::CaDerivations))) {
            if (optOutputPath) {
                outputs[outputName] =
                    localStore->printStorePath(*optOutputPath);
            } else {
                assert(nix::experimentalFeatureSettings.isEnabled(
                    nix::Xp::CaDerivations));
                outputs[outputName] = std::nullopt;
            }
        }
    } catch (const std::exception &e) {
        state.ctx.errors.make<nix::EvalError>(
            "derivation '%s' does not have valid outputs: %s",
            attrPath, e.what()
        ).debugThrow();
    }

    if (args.meta) {
        nlohmann::json meta_;
        for (auto &metaName : drvInfo.queryMetaNames(state)) {
            nix::NixStringContext context;
            std::stringstream ss;

            auto metaValue = drvInfo.queryMeta(state, metaName);
            // Skip non-serialisable types
            // TODO: Fix serialisation of derivations to store paths
            if (metaValue == 0) {
                continue;
            }

            nix::printValueAsJSON(state, true, *metaValue, nix::noPos, ss,
                                  context);

            meta_[metaName] = nlohmann::json::parse(ss.str());
        }
        meta = meta_;
    }
    if (args.checkCacheStatus) {
        cacheStatus = queryIsCached(state.aio, *localStore, outputs)
                          ? Drv::CacheStatus::Cached
                          : Drv::CacheStatus::Uncached;
    } else {
        cacheStatus = Drv::CacheStatus::Unknown;
    }

    drvPath = localStore->printStorePath(drvInfo.requireDrvPath(state));

    auto drv = state.aio.blockOn(localStore->readDerivation(drvInfo.requireDrvPath(state)));
    for (const auto &[inputDrvPath, inputNode] : drv.inputDrvs.map) {
        std::set<std::string> inputDrvOutputs;
        for (auto &outputName : inputNode.value) {
            inputDrvOutputs.insert(outputName);
        }
        inputDrvs[localStore->printStorePath(inputDrvPath)] = inputDrvOutputs;
    }
    name = drvInfo.queryName(state);
    system = drv.platform;
}

void to_json(nlohmann::json &json, const Drv &drv) {
    std::map<std::string, nlohmann::json> outputsJson;
    for (auto &[name, optPath] : drv.outputs) {
        outputsJson[name] =
            optPath ? nlohmann::json(*optPath) : nlohmann::json(nullptr);
    }

    json = nlohmann::json{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", outputsJson},
                          {"inputDrvs", drv.inputDrvs}};

    if (drv.meta.has_value()) {
        json["meta"] = drv.meta.value();
    }

    if (auto constituents = drv.constituents) {
        json["constituents"] = constituents->constituents;
        json["namedConstituents"] = constituents->namedConstituents;
    }

    if (drv.cacheStatus != Drv::CacheStatus::Unknown) {
        json["isCached"] = drv.cacheStatus == Drv::CacheStatus::Cached;
    }
}

void register_gc_root(nix::Path &gcRootsDir, std::string &drvPath, const nix::ref<nix::Store> &store,
                      nix::AsyncIoRoot &aio) {
    if (!gcRootsDir.empty()) {
        nix::Path root =
            gcRootsDir + "/" +
            std::string(nix::baseNameOf(drvPath));
        if (!nix::pathExists(root)) {
            auto localStore =
                store
                    .dynamic_pointer_cast<nix::LocalFSStore>();
            auto storePath =
                localStore->parseStorePath(drvPath);
            aio.blockOn(localStore->addPermRoot(storePath, root));
        }
    }
}
