#include "lix/libstore/builtins.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/strings.hh"

namespace nix {

void builtinFetchurl(const BasicDerivation & drv, const std::string & netrcData, const std::string & caFileData)
{
    /* Make the host's netrc data available. Too bad curl requires
       this to be stored in a file. It would be nice if we could just
       pass a pointer to the data. */
    if (netrcData != "") {
        settings.netrcFile.override("netrc");
        writeFile(settings.netrcFile, netrcData, 0600);
    }

    settings.caFile.override("ca-certificates.crt");
    writeFile(settings.caFile, caFileData, 0600);

    auto getAttr = [&](const std::string & name) {
        auto i = drv.env.find(name);
        if (i == drv.env.end()) throw Error("attribute '%s' missing", name);
        return i->second;
    };

    Path storePath = getAttr("out");
    auto mainUrl = getAttr("url");
    bool unpack = getOr(drv.env, "unpack", "") == "1";

    /* Note: have to use a fresh fileTransfer here because we're in
       a forked process. */
    auto fileTransfer = makeFileTransfer();

    auto fetch = [&](const std::string & url) {

        auto raw = fileTransfer->download(url).second;
        auto decompressor = makeDecompressionSource(
            unpack && mainUrl.ends_with(".xz") ? "xz" : "none", *raw);

        if (unpack)
            restorePath(storePath, *decompressor);
        else
            writeFile(storePath, *decompressor);

        auto executable = drv.env.find("executable");
        if (executable != drv.env.end() && executable->second == "1") {
            if (chmod(storePath.c_str(), 0755) == -1)
                throw SysError("making '%1%' executable", storePath);
        }
    };

    /* Try the hashed mirrors first. */
    if (getAttr("outputHashMode") == "flat")
        for (auto hashedMirror : settings.hashedMirrors.get())
            try {
                if (!hashedMirror.ends_with("/")) hashedMirror += '/';
                std::optional<HashType> ht = parseHashTypeOpt(getAttr("outputHashAlgo"));
                Hash h = newHashAllowEmpty(getAttr("outputHash"), ht);
                fetch(hashedMirror + printHashType(h.type) + "/" + h.to_string(Base::Base16, false));
                return;
            } catch (Error & e) {
                debug(e.what());
            }

    /* Otherwise try the specified URL. */
    fetch(mainUrl);
}

}
