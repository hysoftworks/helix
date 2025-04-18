#pragma once
///@file

#include "lix/libstore/build/derivation-goal.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libutil/processes.hh"

namespace nix {

struct LocalDerivationGoal : public DerivationGoal
{
    LocalStore & getLocalStore();

    /**
     * User selected for running the builder.
     */
    std::unique_ptr<UserLock> buildUser;

    /**
     * The process ID of the builder.
     */
    Pid pid;

    /**
     * The cgroup of the builder, if any.
     */
    std::optional<Path> cgroup;

    /**
     * The temporary directory.
     */
    Path tmpDir;

    /**
     * The path of the temporary directory in the sandbox.
     */
    Path tmpDirInSandbox;

    /**
     * Master side of the pseudoterminal used for the builder's
     * standard output/error.
     */
    AutoCloseFD builderOutPTY;

    /**
     * Pipe for synchronising updates to the builder namespaces.
     */
    Pipe userNamespaceSync;

    /**
     * The mount namespace and user namespace of the builder, used to add additional
     * paths to the sandbox as a result of recursive Nix calls.
     */
    AutoCloseFD sandboxMountNamespace;
    AutoCloseFD sandboxUserNamespace;

    /**
     * On Linux, whether we're doing the build in its own user
     * namespace.
     */
    bool usingUserNamespace = true;

    /**
     * Whether we're currently doing a chroot build.
     */
    bool useChroot = false;

    Path chrootRootDir;

    /**
     * RAII object to delete the chroot directory.
     */
    std::shared_ptr<AutoDelete> autoDelChroot;

    /**
     * Whether to run the build in a private network namespace.
     */
    bool privateNetwork = false;

    /**
     * Stuff we need to pass to initChild().
     */
    struct ChrootPath {
        Path source;
        bool optional;
        ChrootPath(Path source = "", bool optional = false)
            : source(source), optional(optional)
        { }
    };
    typedef map<Path, ChrootPath> PathsInChroot; // maps target path to source path
    PathsInChroot pathsInChroot;

    typedef map<std::string, std::string> Environment;
    Environment env;

#if __APPLE__
    typedef std::string SandboxProfile;
    SandboxProfile additionalSandboxProfile;
#endif

    /**
     * Hash rewriting.
     */
    StringMap inputRewrites, outputRewrites;
    typedef map<StorePath, StorePath> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    /**
     * The outputs paths used during the build.
     *
     * - Input-addressed derivations or fixed content-addressed outputs are
     *   sometimes built when some of their outputs already exist, and can not
     *   be hidden via sandboxing. We use temporary locations instead and
     *   rewrite after the build. Otherwise the regular predetermined paths are
     *   put here.
     *
     * - Floating content-addressed derivations do not know their final build
     *   output paths until the outputs are hashed, so random locations are
     *   used, and then renamed. The randomness helps guard against hidden
     *   self-references.
     */
    OutputPathMap scratchOutputs;

    /**
     * Path registration info from the previous round, if we're
     * building multiple times. Since this contains the hash, it
     * allows us to compare whether two rounds produced the same
     * result.
     */
    std::map<Path, ValidPathInfo> prevInfos;

    uid_t sandboxUid() { return usingUserNamespace ? (!buildUser || buildUser->getUIDCount() == 1 ? 1000 : 0) : buildUser->getUID(); }
    gid_t sandboxGid() { return usingUserNamespace ? (!buildUser || buildUser->getUIDCount() == 1 ? 100  : 0) : buildUser->getGID(); }

    const static Path homeDir;

    /**
     * The recursive Nix daemon socket.
     */
    AutoCloseFD daemonSocket;

    /**
     * The daemon main thread.
     */
    std::thread daemonThread;

    /**
     * The daemon worker threads.
     */
    std::vector<std::thread> daemonWorkerThreads;

    /**
     * Paths that were added via recursive Nix calls.
     */
    StorePathSet addedPaths;

    /**
     * Realisations that were added via recursive Nix calls.
     */
    std::set<DrvOutput> addedDrvOutputs;

    /**
     * Recursive Nix calls are only allowed to build or realize paths
     * in the original input closure or added via a recursive Nix call
     * (so e.g. you can't do 'nix-store -r /nix/store/<bla>' where
     * /nix/store/<bla> is some arbitrary path in a binary cache).
     */
    bool isAllowed(const StorePath & path)
    {
        return inputPaths.count(path) || addedPaths.count(path);
    }
    bool isAllowed(const DrvOutput & id)
    {
        return addedDrvOutputs.count(id);
    }

    bool isAllowed(const DerivedPath & req);

    friend struct RestrictedStore;

    /**
     * Create a LocalDerivationGoal without an on-disk .drv file,
     * possibly a platform-specific subclass
     */
    static std::unique_ptr<LocalDerivationGoal> makeLocalDerivationGoal(
        const StorePath & drvPath,
        const OutputsSpec & wantedOutputs,
        Worker & worker,
        bool isDependency,
        BuildMode buildMode
    );

    /**
     * Create a LocalDerivationGoal for an on-disk .drv file,
     * possibly a platform-specific subclass
     */
    static std::unique_ptr<LocalDerivationGoal> makeLocalDerivationGoal(
        DrvHasRoot drvRoot,
        const StorePath & drvPath,
        const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs,
        Worker & worker,
        bool isDependency,
        BuildMode buildMode
    );

    virtual ~LocalDerivationGoal() noexcept(false) override;

    /**
     * Whether we need to perform hash rewriting if there are valid output paths.
     */
   virtual bool needsHashRewrite();

    /**
     * The additional states.
     */
    kj::Promise<Result<WorkResult>> tryLocalBuild() noexcept override;

    /**
     * Start building a derivation.
     */
    kj::Promise<Result<void>> startBuilder();

    /**
     * Fill in the environment for the builder.
     */
    void initEnv();

    /**
     * Setup tmp dir location.
     */
    void initTmpDir();

    /**
     * Write a JSON file containing the derivation attributes.
     */
    kj::Promise<Result<void>> writeStructuredAttrs();

    void startDaemon();

    void stopDaemon();

    /**
     * Add 'path' to the set of paths that may be referenced by the
     * outputs, and make it appear in the sandbox.
     */
    void addDependency(const StorePath & path);

    /**
     * Make a file owned by the builder.
     */
    void chownToBuilder(const Path & path);

    int getChildStatus() override;

    /**
     * Run the builder's process.
     */
    void runChild();

    /**
     * Check that the derivation outputs all exist and register them
     * as valid.
     */
    kj::Promise<Result<SingleDrvOutputs>> registerOutputs() override;

    void signRealisation(Realisation &) override;

    /**
     * Check that an output meets the requirements specified by the
     * 'outputChecks' attribute (or the legacy
     * '{allowed,disallowed}{References,Requisites}' attributes).
     */
    kj::Promise<Result<void>> checkOutputs(const std::map<std::string, ValidPathInfo> & outputs, const std::map<std::string, StorePath> & alreadyRegisteredOutputs);

    /**
     * Close the read side of the logger pipe.
     */
    void closeReadPipes() override;

    /**
     * Cleanup hooks for buildDone()
     */
    void cleanupHookFinally() override;
    void cleanupPreChildKill() override;
    void cleanupPostChildKill() override;
    bool cleanupDecideWhetherDiskFull() override;
    void cleanupPostOutputsRegisteredModeCheck() override;
    void cleanupPostOutputsRegisteredModeNonCheck() override;

    /**
     * Delete the temporary directory, if we have one.
     */
    void deleteTmpDir(bool force, bool duringDestruction = false);

    /**
     * Forcibly kill the child process, if any.
     *
     * Called by destructor, can't be overridden
     */
    void killChild() override final;

    /**
     * Kill any processes running under the build user UID or in the
     * cgroup of the build.
     */
    virtual void killSandbox(bool getStats);

    /**
     * Create alternative path calculated from but distinct from the
     * input, so we can avoid overwriting outputs (or other store paths)
     * that already exist.
     */
    StorePath makeFallbackPath(const StorePath & path);

    /**
     * Make a path to another based on the output name along with the
     * derivation hash.
     *
     * @todo Add option to randomize, so we can audit whether our
     * rewrites caught everything
     */
    StorePath makeFallbackPath(OutputNameView outputName);

protected:
    using DerivationGoal::DerivationGoal;

    /**
     * Setup dependencies outside the sandbox.
     * Called in the parent nix process.
     */
    virtual void prepareSandbox()
    {
        throw Error("sandboxing builds is not supported on this platform");
    };

    /**
     * Create a new process that runs `openSlave` and `runChild`
     * On some platforms this process is created with sandboxing flags.
     */
    virtual Pid startChild(std::function<void()> openSlave);

    /**
     * Set up the system call filtering required for the sandbox.
     * This currently only has an effect on Linux.
     */
    virtual void setupSyscallFilter() {}

    /**
     * Execute the builder, replacing the current process.
     * Generally this means an `execve` call.
     */
    virtual void execBuilder(std::string builder, Strings args, Strings envStrs);

    /**
     * Whether derivation can be built on current platform with `uid-range` feature
     */
    virtual bool supportsUidRange()
    {
        return false;
    }

    virtual bool respectsTimeouts() override
    {
        return true;
    }
};

}
