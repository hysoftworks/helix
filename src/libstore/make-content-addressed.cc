#include "make-content-addressed.hh"
#include "references.hh"

namespace nix {

std::map<StorePath, StorePath> makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths)
{
    StorePathSet closure;
    srcStore.computeFSClosure(storePaths, closure);

    auto paths = srcStore.topoSortPaths(closure);

    std::reverse(paths.begin(), paths.end());

    std::map<StorePath, StorePath> remappings;

    for (auto & path : paths) {
        auto pathS = srcStore.printStorePath(path);
        auto oldInfo = srcStore.queryPathInfo(path);
        std::string oldHashPart(path.hashPart());

        StringSink sink;
        sink << srcStore.narFromPath(path);

        StringMap rewrites;

        StoreReferences refs;
        for (auto & ref : oldInfo->references) {
            if (ref == path)
                refs.self = true;
            else {
                auto i = remappings.find(ref);
                auto replacement = i != remappings.end() ? i->second : ref;
                // FIXME: warn about unremapped paths?
                if (replacement != ref)
                    rewrites.insert_or_assign(srcStore.printStorePath(ref), srcStore.printStorePath(replacement));
                refs.others.insert(std::move(replacement));
            }
        }

        sink.s = rewriteStrings(sink.s, rewrites);

        auto narModuloHash = [&] {
            StringSource source{sink.s};
            return computeHashModulo(htSHA256, oldHashPart, source).first;
        }();

        ValidPathInfo info {
            dstStore,
            path.name(),
            FixedOutputInfo {
                .method = FileIngestionMethod::Recursive,
                .hash = narModuloHash,
                .references = std::move(refs),
            },
            Hash::dummy,
        };

        printInfo("rewriting '%s' to '%s'", pathS, dstStore.printStorePath(info.path));

        const auto rewritten = rewriteStrings(sink.s, {{oldHashPart, std::string(info.path.hashPart())}});

        info.narHash = hashString(htSHA256, rewritten);
        info.narSize = sink.s.size();

        StringSource source(rewritten);
        dstStore.addToStore(info, source);

        remappings.insert_or_assign(std::move(path), std::move(info.path));
    }

    return remappings;
}

StorePath makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePath & fromPath)
{
    auto remappings = makeContentAddressed(srcStore, dstStore, StorePathSet { fromPath });
    auto i = remappings.find(fromPath);
    assert(i != remappings.end());
    return i->second;
}

}
