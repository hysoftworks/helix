///@file

#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libutil/finally.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/daemon.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "daemon.hh"

#include <algorithm>
#include <climits>
#include <cstring>

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>

#if __APPLE__ || __FreeBSD__
#include <sys/ucred.h>
#endif
#if __APPLE__
#include <membership.h>
#endif

namespace nix {

using namespace nix::daemon;

/**
 * Settings related to authenticating clients for the Nix daemon.
 *
 * For pipes we have little good information about the client side, but
 * for Unix domain sockets we do. So currently these options implemented
 * mandatory access control based on user names and group names (looked
 * up and translated to UID/GIDs in the CLI process that runs the code
 * in this file).
 *
 * No code outside of this file knows about these settings (this is not
 * exposed in a header); all authentication and authorization happens in
 * `daemon.cc`.
 */
struct AuthorizationSettings : Config {
    #include "daemon-settings.gen.inc"
};

AuthorizationSettings authorizationSettings;

static GlobalConfig::Register rSettings(&authorizationSettings);

#ifndef __linux__
#define SPLICE_F_MOVE 0
static ssize_t splice(int fd_in, void *off_in, int fd_out, void *off_out, size_t len, unsigned int flags)
{
    // We ignore most parameters, we just have them for conformance with the linux syscall
    std::vector<char> buf(8192);
    auto read_count = read(fd_in, buf.data(), buf.size());
    if (read_count == -1)
        return read_count;
    auto write_count = decltype(read_count)(0);
    while (write_count < read_count) {
        auto res = write(fd_out, buf.data() + write_count, read_count - write_count);
        if (res == -1)
            return res;
        write_count += res;
    }
    return read_count;
}
#endif


static void sigChldHandler(int sigNo)
{
    // Ensure we don't modify errno of whatever we've interrupted
    auto saved_errno = errno;
    //  Reap all dead children.
    while (waitpid(-1, 0, WNOHANG) > 0) ;
    errno = saved_errno;
}


static void setSigChldAction(bool autoReap)
{
    struct sigaction act, oact;
    act.sa_handler = autoReap ? sigChldHandler : SIG_DFL;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGCHLD, &act, &oact))
        throw SysError("setting SIGCHLD handler");
}

/**
 * @return Is the given user a member of this group?
 *
 * @param user User specified by username.
 *
 * @param group Group the user might be a member of.
 */
static bool matchUser(const std::string & user, const struct group & gr)
{
    for (char * * mem = gr.gr_mem; *mem; mem++)
        if (user == *mem) return true;
#if __APPLE__
    // FIXME: we should probably pipe the uid through these functions
    // instead of converting the username back into the uid
    if (auto pw = getpwnam(user.c_str())) {
        uuid_t uuid, gruuid;
        if (!mbr_uid_to_uuid(pw->pw_uid, uuid) && !mbr_gid_to_uuid(gr.gr_gid, gruuid)) {
            int ismember = 0;
            if (!mbr_check_membership(uuid, gruuid, &ismember)) {
                return !!ismember;
            }
        }
    }
#endif
    return false;
}


/**
 * Does the given user (specified by user name and primary group name)
 * match the given user/group whitelist?
 *
 * If the list allows all users: Yes.
 *
 * If the username is in the set: Yes.
 *
 * If the groupname is in the set: Yes.
 *
 * If the user is in another group which is in the set: yes.
 *
 * Otherwise: No.
 */
static bool matchUser(const std::string & user, const std::string & group, const Strings & users)
{
    if (find(users.begin(), users.end(), "*") != users.end())
        return true;

    if (find(users.begin(), users.end(), user) != users.end())
        return true;

    for (auto & i : users)
        if (i.substr(0, 1) == "@") {
            if (group == i.substr(1)) return true;
            struct group * gr = getgrnam(i.c_str() + 1);
            if (!gr) continue;
            if (matchUser(user, *gr)) return true;
        }

    return false;
}


struct PeerInfo
{
    bool pidKnown;
    pid_t pid;
    bool uidKnown;
    uid_t uid;
    bool gidKnown;
    gid_t gid;
};


/**
 * Get the identity of the caller, if possible.
 */
static PeerInfo getPeerInfo(int remote)
{
    PeerInfo peer = { false, 0, false, 0, false, 0 };

#if defined(SO_PEERCRED)

    ucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == -1)
        throw SysError("getting peer credentials");
    peer = { true, cred.pid, true, cred.uid, true, cred.gid };

#elif defined(LOCAL_PEERCRED)

#if !defined(SOL_LOCAL)
#define SOL_LOCAL 0
#endif

    xucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_LOCAL, LOCAL_PEERCRED, &cred, &credLen) == -1)
        throw SysError("getting peer credentials");
    peer = { false, 0, true, cred.cr_uid, true, cred.cr_gid };

#if defined(LOCAL_PEERPID)
    socklen_t pidLen = sizeof(peer.pid);
    if (!getsockopt(remote, SOL_LOCAL, LOCAL_PEERPID, &peer.pid, &pidLen))
        peer.pidKnown = true;
#endif

#endif

    return peer;
}


#define SD_LISTEN_FDS_START 3


/**
 * Open a store without a path info cache.
 */
static kj::Promise<Result<ref<Store>>> openUncachedStore()
try {
    StoreConfig::Params params; // FIXME: get params from somewhere
    // Disable caching since the client already does that.
    params["path-info-cache-size"] = "0";
    co_return TRY_AWAIT(openStore(settings.storeUri, params));
} catch (...) {
    co_return result::current_exception();
}

/**
 * Authenticate a potential client
 *
 * @param peer Information about other end of the connection, the client which
 * wants to communicate with us.
 *
 * @return A pair of a `TrustedFlag`, whether the potential client is trusted,
 * and the name of the user (useful for printing messages).
 *
 * If the potential client is not allowed to talk to us, we throw an `Error`.
 */
static std::pair<TrustedFlag, std::string> authPeer(const PeerInfo & peer)
{
    TrustedFlag trusted = NotTrusted;

    struct passwd * pw = peer.uidKnown ? getpwuid(peer.uid) : 0;
    std::string user = pw ? pw->pw_name : std::to_string(peer.uid);

    struct group * gr = peer.gidKnown ? getgrgid(peer.gid) : 0;
    std::string group = gr ? gr->gr_name : std::to_string(peer.gid);

    const Strings & trustedUsers = authorizationSettings.trustedUsers;
    const Strings & allowedUsers = authorizationSettings.allowedUsers;

    if (matchUser(user, group, trustedUsers))
        trusted = Trusted;

    if ((!trusted && !matchUser(user, group, allowedUsers)) || group == settings.buildUsersGroup)
        throw Error("user '%1%' is not allowed to connect to the Nix daemon", user);

    return { trusted, std::move(user) };
}


/**
 * Run a server. The loop opens a socket and accepts new connections from that
 * socket.
 *
 * @param forceTrustClientOpt If present, force trusting or not trusted
 * the client. Otherwise, decide based on the authentication settings
 * and user credentials (from the unix domain socket).
 */
static void daemonLoop(AsyncIoRoot & aio, std::optional<TrustedFlag> forceTrustClientOpt);
static void daemonLoopImpl(std::optional<TrustedFlag> forceTrustClientOpt)
{
    if (chdir("/") == -1)
        throw SysError("cannot change current directory");

    AutoCloseFD fdSocket;

    //  Handle socket-based activation by systemd.
    auto listenFds = getEnv("LISTEN_FDS");
    if (listenFds) {
        if (getEnv("LISTEN_PID") != std::to_string(getpid()) || listenFds != "1")
            throw Error("unexpected systemd environment variables");
        fdSocket = AutoCloseFD{SD_LISTEN_FDS_START};
        closeOnExec(fdSocket.get());
    }

    //  Otherwise, create and bind to a Unix domain socket.
    else {
        createDirs(dirOf(settings.nixDaemonSocketFile));
        fdSocket = createUnixDomainSocket(settings.nixDaemonSocketFile, 0666);
    }

    //  Get rid of children automatically; don't let them become zombies.
    setSigChldAction(true);

    //  Loop accepting connections.
    while (1) {

        try {
            //  Accept a connection.
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote{accept(fdSocket.get(),
                    reinterpret_cast<struct sockaddr *>(&remoteAddr), &remoteAddrLen)};
            checkInterrupt();
            if (!remote) {
                if (errno == EINTR) continue;
                throw SysError("accepting connection");
            }

            closeOnExec(remote.get());

            PeerInfo peer { .pidKnown = false };
            TrustedFlag trusted;
            std::string user;

            if (forceTrustClientOpt)
                trusted = *forceTrustClientOpt;
            else {
                peer = getPeerInfo(remote.get());
                auto [_trusted, _user] = authPeer(peer);
                trusted = _trusted;
                user = _user;
            };

            printInfo((std::string) "accepted connection from pid %1%, user %2%" + (trusted ? " (trusted)" : ""),
                peer.pidKnown ? std::to_string(peer.pid) : "<unknown>",
                peer.uidKnown ? user : "<unknown>");

            //  Fork a child to handle the connection.
            ProcessOptions options;
            options.errorPrefix = "unexpected Nix daemon error: ";
            options.dieWithParent = false;
            options.runExitHandlers = true;
            startProcess([&]() {
                fdSocket.reset();

                //  Background the daemon.
                if (setsid() == -1)
                    throw SysError("creating a new session");

                AsyncIoRoot aio;

                // Restart the signal handler thread since it met its untimely
                // demise at fork time.
                startSignalHandlerThread(DoSignalSave::DontSaveBecauseAdvancedProcess);

                //  Restore normal handling of SIGCHLD.
                setSigChldAction(false);

                //  For debugging, stuff the pid into argv[1].
                if (peer.pidKnown && savedArgv[1]) {
                    auto processName = std::to_string(peer.pid);
                    strncpy(savedArgv[1], processName.c_str(), strlen(savedArgv[1]));
                }

                //  Handle the connection.
                FdSource from(remote.get());
                FdSink to(remote.get());
                processConnection(
                    aio, aio.blockOn(openUncachedStore()), from, to, trusted, NotRecursive
                );

                exit(0);
            }, options).release();

        } catch (Interrupted & e) {
            return;
        } catch (Error & error) {
            auto ei = error.info();
            // FIXME: add to trace?
            ei.msg = HintFmt("error processing connection: %1%", ei.msg.str());
            logError(ei);
        }
    }
}
static void daemonLoop(AsyncIoRoot & aio, std::optional<TrustedFlag> forceTrustClientOpt)
{
    // we can't reuse the external async io root since it'd be shared with the
    // children we will create, potentially trashing state, but the *previous*
    // root is still alive as far as kj is concerned. we cannot recreate it in
    // the child easily because darwin closes kqueues after fork, and since kj
    // asserts after the kqueue close returns EBADF we'll die. the least awful
    // way around this is to run the daemon loop in its own thread, without an
    // async io root, and thus not have any shared state after we have forked.
    std::async(std::launch::async, [&] {
        ReceiveInterrupts ri;
        return daemonLoopImpl(forceTrustClientOpt);
    }).get();
}

/**
 * Forward a standard IO connection to the given remote store.
 *
 * We just act as a middleman blindly ferry output between the standard
 * input/output and the remote store connection, not processing anything.
 *
 * Loops until standard input disconnects, or an error is encountered.
 */
static void forwardStdioConnection(RemoteStore & store) {
    auto conn = store.openConnectionWrapper();
    int from = conn->from.fd;
    int to = conn->to.fd;

    auto nfds = std::max(from, STDIN_FILENO) + 1;
    while (true) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(from, &fds);
        FD_SET(STDIN_FILENO, &fds);
        if (select(nfds, &fds, nullptr, nullptr, nullptr) == -1)
            throw SysError("waiting for data from client or server");
        if (FD_ISSET(from, &fds)) {
            auto res = splice(from, nullptr, STDOUT_FILENO, nullptr, SSIZE_MAX, SPLICE_F_MOVE);
            if (res == -1)
                throw SysError("splicing data from daemon socket to stdout");
            else if (res == 0)
                throw EndOfFile("unexpected EOF from daemon socket");
        }
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            auto res = splice(STDIN_FILENO, nullptr, to, nullptr, SSIZE_MAX, SPLICE_F_MOVE);
            if (res == -1)
                throw SysError("splicing data from stdin to daemon socket");
            else if (res == 0)
                return;
        }
    }
}

/**
 * Process a client connecting to us via standard input/output
 *
 * Unlike `forwardStdioConnection()` we do process commands ourselves in
 * this case, not delegating to another daemon.
 *
 * @param trustClient Whether to trust the client. Forwarded directly to
 * `processConnection()`.
 */
static void
processStdioConnection(AsyncIoRoot & aio, ref<Store> store, TrustedFlag trustClient)
{
    FdSource from(STDIN_FILENO);
    FdSink to(STDOUT_FILENO);
    processConnection(aio, store, from, to, trustClient, NotRecursive);
}

/**
 * Entry point shared between the new CLI `nix daemon` and old CLI
 * `nix-daemon`.
 *
 * @param forceTrustClientOpt See `daemonLoop()` and the parameter with
 * the same name over there for details.
 */
static void
runDaemon(AsyncIoRoot & aio, bool stdio, std::optional<TrustedFlag> forceTrustClientOpt)
{
    if (stdio) {
        auto store = aio.blockOn(openUncachedStore());

        // If --force-untrusted is passed, we cannot forward the connection and
        // must process it ourselves (before delegating to the next store) to
        // force untrusting the client.
        if (auto remoteStore = store.try_cast_shared<RemoteStore>(); remoteStore && (!forceTrustClientOpt || *forceTrustClientOpt != NotTrusted))
            forwardStdioConnection(*remoteStore);
        else
            // `Trusted` is passed in the auto (no override case) because we
            // cannot see who is on the other side of a plain pipe. Limiting
            // access to those is explicitly not `nix-daemon`'s responsibility.
            processStdioConnection(aio, store, forceTrustClientOpt.value_or(Trusted));
    } else
        daemonLoop(aio, forceTrustClientOpt);
}

static int main_nix_daemon(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        auto stdio = false;
        std::optional<TrustedFlag> isTrustedOpt = std::nullopt;

        LegacyArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--daemon")
                ; //  ignored for backwards compatibility
            else if (*arg == "--help")
                showManPage("nix-daemon");
            else if (*arg == "--version")
                printVersion("nix-daemon");
            else if (*arg == "--stdio")
                stdio = true;
            else if (*arg == "--force-trusted") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = Trusted;
            } else if (*arg == "--force-untrusted") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = NotTrusted;
            } else if (*arg == "--default-trust") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = std::nullopt;
            } else return false;
            return true;
        }).parseCmdline(argv);

        runDaemon(aio, stdio, isTrustedOpt);

        return 0;
    }
}

void registerLegacyNixDaemon() {
    LegacyCommandRegistry::add("nix-daemon", main_nix_daemon);
}

struct CmdDaemon : StoreCommand
{
    bool stdio = false;
    std::optional<TrustedFlag> isTrustedOpt = std::nullopt;

    CmdDaemon()
    {
        addFlag({
            .longName = "stdio",
            .description = "Attach to standard I/O, instead of trying to bind to a UNIX socket.",
            .handler = {&stdio, true},
        });

        addFlag({
            .longName = "force-trusted",
            .description = "Force the daemon to trust connecting clients.",
            .handler = {[&]() {
                isTrustedOpt = Trusted;
            }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });

        addFlag({
            .longName = "force-untrusted",
            .description = "Force the daemon to not trust connecting clients. The connection will be processed by the receiving daemon before forwarding commands.",
            .handler = {[&]() {
                isTrustedOpt = NotTrusted;
            }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });

        addFlag({
            .longName = "default-trust",
            .description = "Use Nix's default trust.",
            .handler = {[&]() {
                isTrustedOpt = std::nullopt;
            }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });
    }

    std::string description() override
    {
        return "daemon to perform store operations on behalf of non-root clients";
    }

    Category category() override { return catUtility; }

    std::string doc() override
    {
        return
          #include "daemon.md"
          ;
    }

    void run(ref<Store> store) override
    {
        runDaemon(aio(), stdio, isTrustedOpt);
    }
};

void registerNixDaemon()
{
    registerCommand2<CmdDaemon>({"daemon"});
}

}
