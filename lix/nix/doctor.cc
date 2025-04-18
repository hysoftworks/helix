#include <sstream>

#include "lix/libcmd/command.hh"
#include "lix/libutil/logging.hh"
#include "lix/libstore/serve-protocol.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libutil/exit.hh"
#include "doctor.hh"

namespace nix {

namespace {

std::string formatProtocol(unsigned int proto)
{
    if (proto) {
        auto major = GET_PROTOCOL_MAJOR(proto) >> 8;
        auto minor = GET_PROTOCOL_MINOR(proto);
        return fmt("%1%.%2%", major, minor);
    }
    return "unknown";
}

bool checkPass(const std::string & msg) {
    notice(ANSI_GREEN "[PASS] " ANSI_NORMAL + msg);
    return true;
}

bool checkFail(const std::string & msg) {
    notice(ANSI_RED "[FAIL] " ANSI_NORMAL + msg);
    return false;
}

void checkInfo(const std::string & msg) {
    notice(ANSI_BLUE "[INFO] " ANSI_NORMAL + msg);
}

}

struct CmdDoctor : StoreCommand
{
    bool success = true;

    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::string description() override
    {
        return "check your system for potential problems and print a PASS or FAIL for each check";
    }

    Category category() override { return catNixInstallation; }

    void run(ref<Store> store) override
    {
        logger->log("Running checks against store uri: " + store->getUri());

        if (store.try_cast_shared<LocalFSStore>()) {
            success &= checkNixInPath();
            success &= checkProfileRoots(store);
        }
        success &= checkStoreProtocol(aio().blockOn(store->getProtocol()));
        checkTrustedUser(store);

        if (!success)
            throw Exit(2);
    }

    bool checkNixInPath()
    {
        PathSet dirs;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH").value_or(""), ":"))
            if (pathExists(dir + "/nix-env"))
                dirs.insert(dirOf(canonPath(dir + "/nix-env", true)));

        if (dirs.size() != 1) {
            std::stringstream ss;
            ss << "Multiple versions of nix found in PATH:\n";
            for (auto & dir : dirs)
                ss << "  " << dir << "\n";
            return checkFail(ss.str());
        }

        return checkPass("PATH contains only one nix version.");
    }

    bool checkProfileRoots(ref<Store> store)
    {
        PathSet dirs;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH").value_or(""), ":")) {
            Path profileDir = dirOf(dir);
            try {
                Path userEnv = canonPath(profileDir, true);

                if (store->isStorePath(userEnv) && userEnv.ends_with("user-environment")) {
                    while (profileDir.find("/profiles/") == std::string::npos && isLink(profileDir))
                        profileDir = absPath(readLink(profileDir), dirOf(profileDir));

                    if (profileDir.find("/profiles/") == std::string::npos)
                        dirs.insert(dir);
                }
            } catch (SysError &) {}
        }

        if (!dirs.empty()) {
            std::stringstream ss;
            ss << "Found profiles outside of " << settings.nixStateDir << "/profiles.\n"
               << "The generation this profile points to might not have a gcroot and could be\n"
               << "garbage collected, resulting in broken symlinks.\n\n";
            for (auto & dir : dirs)
                ss << "  " << dir << "\n";
            ss << "\n";
            return checkFail(ss.str());
        }

        return checkPass("All profiles are gcroots.");
    }

    bool checkStoreProtocol(unsigned int storeProto)
    {
        unsigned int clientProto = GET_PROTOCOL_MAJOR(SERVE_PROTOCOL_VERSION) == GET_PROTOCOL_MAJOR(storeProto)
            ? SERVE_PROTOCOL_VERSION
            : PROTOCOL_VERSION;

        if (clientProto != storeProto) {
            std::stringstream ss;
            ss << "Warning: protocol version of this client does not match the store.\n"
               << "While this is not necessarily a problem it's recommended to keep the client in\n"
               << "sync with the daemon.\n\n"
               << "Client protocol: " << formatProtocol(clientProto) << "\n"
               << "Store protocol: " << formatProtocol(storeProto) << "\n\n";
            return checkFail(ss.str());
        }

        return checkPass("Client protocol matches store protocol.");
    }

    void checkTrustedUser(ref<Store> store)
    {
        auto trustedMay = aio().blockOn(store->isTrustedClient());
        std::string_view trustedness = trustedMay ? (*trustedMay ? "trusted" : "not trusted") : "unknown trust";
        checkInfo(fmt("You are %s by store uri: %s", trustedness, store->getUri()));
    }
};

void registerNixDoctor()
{
    registerCommand<CmdDoctor>("doctor");
}

}
