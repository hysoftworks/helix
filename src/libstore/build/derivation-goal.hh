#pragma once
///@file

#include "notifying-counter.hh"
#include "parsed-derivations.hh"
#include "lock.hh"
#include "outputs-spec.hh"
#include "store-api.hh"
#include "pathlocks.hh"
#include "goal.hh"
#include <kj/time.h>

namespace nix {

using std::map;

struct HookInstance;

struct HookReplyBase {
    struct [[nodiscard]] Accept {
        kj::Promise<Outcome<void, Goal::Finished>> promise;
    };
    struct [[nodiscard]] Decline {};
    struct [[nodiscard]] Postpone {};
};

struct [[nodiscard]] HookReply
    : HookReplyBase,
      std::variant<HookReplyBase::Accept, HookReplyBase::Decline, HookReplyBase::Postpone>
{
    HookReply() = delete;
    using variant::variant;
};

/**
 * Unless we are repairing, we don't both to test validity and just assume it,
 * so the choices are `Absent` or `Valid`.
 */
enum struct PathStatus {
    Corrupt,
    Absent,
    Valid,
};

struct InitialOutputStatus {
    StorePath path;
    PathStatus status;
    /**
     * Valid in the store, and additionally non-corrupt if we are repairing
     */
    bool isValid() const {
        return status == PathStatus::Valid;
    }
    /**
     * Merely present, allowed to be corrupt
     */
    bool isPresent() const {
        return status == PathStatus::Corrupt
            || status == PathStatus::Valid;
    }
};

struct InitialOutput {
    bool wanted;
    Hash outputHash;
    std::optional<InitialOutputStatus> known;
};

/**
 * A goal for building some or all of the outputs of a derivation.
 */
struct DerivationGoal : public Goal
{
    struct InputStream;

    /**
     * Whether to use an on-disk .drv file.
     */
    bool useDerivation;

    /** The path of the derivation. */
    StorePath drvPath;

    /**
     * The goal for the corresponding resolved derivation
     */
    std::shared_ptr<DerivationGoal> resolvedDrvGoal;

    /**
     * The specific outputs that we need to build.
     */
    OutputsSpec wantedOutputs;

    /**
     * Mapping from input derivations + output names to actual store
     * paths. This is filled in by waiteeDone() as each dependency
     * finishes, before inputsRealised() is reached.
     */
    std::map<std::pair<StorePath, std::string>, StorePath> inputDrvOutputs;

    /**
     * See `needRestart`; just for that field.
     */
    enum struct NeedRestartForMoreOutputs {
        /**
         * The goal state machine is progressing based on the current value of
         * `wantedOutputs. No actions are needed.
         */
        OutputsUnmodifedDontNeed,
        /**
         * `wantedOutputs` has been extended, but the state machine is
         * proceeding according to its old value, so we need to restart.
         */
        OutputsAddedDoNeed,
        /**
         * The goal state machine has progressed to the point of doing a build,
         * in which case all outputs will be produced, so extensions to
         * `wantedOutputs` no longer require a restart.
         */
        BuildInProgressWillNotNeed,
    };

    /**
     * Whether additional wanted outputs have been added.
     */
    NeedRestartForMoreOutputs needRestart = NeedRestartForMoreOutputs::OutputsUnmodifedDontNeed;

    bool anyHashMismatchSeen = false;
    bool anyCheckMismatchSeen = false;

    /**
     * See `retrySubstitution`; just for that field.
     */
    enum RetrySubstitution {
        /**
         * No issues have yet arose, no need to restart.
         */
        NoNeed,
        /**
         * Something failed and there is an incomplete closure. Let's retry
         * substituting.
         */
        YesNeed,
        /**
         * We are current or have already retried substitution, and whether or
         * not something goes wrong we will not retry again.
         */
        AlreadyRetried,
    };

    /**
     * Whether to retry substituting the outputs after building the
     * inputs. This is done in case of an incomplete closure.
     */
    RetrySubstitution retrySubstitution = RetrySubstitution::NoNeed;

    /**
     * The derivation stored at drvPath.
     */
    std::unique_ptr<Derivation> drv;

    std::unique_ptr<ParsedDerivation> parsedDrv;

    /**
     * The remainder is state held during the build.
     */

    /**
     * Locks on (fixed) output paths.
     */
    PathLocks outputLocks;

    /**
     * All input paths (that is, the union of FS closures of the
     * immediate input paths).
     */
    StorePathSet inputPaths;

    std::map<std::string, InitialOutput> initialOutputs;

    /**
     * File descriptor for the log file.
     */
    AutoCloseFD fdLogFile;
    std::shared_ptr<BufferedSink> logFileSink, logSink;

    /**
     * Number of bytes received from the builder's stdout/stderr.
     */
    unsigned long logSize;

    /**
     * The most recent log lines.
     */
    std::list<std::string> logTail;

    std::string currentLogLine;
    size_t currentLogLinePos = 0; // to handle carriage return

    std::string currentHookLine;

    /**
     * The build hook.
     */
    std::unique_ptr<HookInstance> hook;

    /**
      * Builder output is pulled from this file descriptor when not null.
      * Owned by the derivation goal or subclass, must not be reset until
      * the build has finished and no more output must be processed by us
      */
    AutoCloseFD * builderOutFD = nullptr;

    /**
     * The sort of derivation we are building.
     */
    std::optional<DerivationType> derivationType;

    typedef kj::Promise<Result<WorkResult>> (DerivationGoal::*GoalState)(bool inBuildSlot) noexcept;
    GoalState state;

    BuildMode buildMode;

    NotifyingCounter<uint64_t>::Bump mcExpectedBuilds, mcRunningBuilds;

    std::unique_ptr<Activity> act;

    /**
     * Activity that denotes waiting for a lock.
     */
    std::unique_ptr<Activity> actLock;

    std::map<ActivityId, Activity> builderActivities;

    /**
     * The remote machine on which we're building.
     */
    std::string machineName;

    DerivationGoal(const StorePath & drvPath,
        const OutputsSpec & wantedOutputs, Worker & worker, bool isDependency,
        BuildMode buildMode = bmNormal);
    DerivationGoal(const StorePath & drvPath, const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs, Worker & worker, bool isDependency,
        BuildMode buildMode = bmNormal);
    virtual ~DerivationGoal() noexcept(false);

    Finished timedOut(Error && ex);

    std::string key() override;

    kj::Promise<Result<WorkResult>> work(bool inBuildSlot) noexcept override;

    /**
     * Add wanted outputs to an already existing derivation goal.
     */
    void addWantedOutputs(const OutputsSpec & outputs);

    /**
     * The states.
     */
    kj::Promise<Result<WorkResult>> getDerivation(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> loadDerivation(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> haveDerivation(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> outputsSubstitutionTried(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> gaveUpOnSubstitution(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> closureRepaired(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> inputsRealised(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> tryToBuild(bool inBuildSlot) noexcept;
    virtual kj::Promise<Result<WorkResult>> tryLocalBuild(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> buildDone(bool inBuildSlot) noexcept;

    kj::Promise<Result<WorkResult>> resolvedFinished(bool inBuildSlot) noexcept;

    /**
     * Is the build hook willing to perform the build?
     */
    HookReply tryBuildHook(bool inBuildSlot);

    virtual int getChildStatus();

    /**
     * Check that the derivation outputs all exist and register them
     * as valid.
     */
    virtual SingleDrvOutputs registerOutputs();

    /**
     * Open a log file and a pipe to it.
     */
    Path openLogFile();

    /**
     * Sign the newly built realisation if the store allows it
     */
    virtual void signRealisation(Realisation&) {}

    /**
     * Close the log file.
     */
    void closeLogFile();

    /**
     * Close the read side of the logger pipe.
     */
    virtual void closeReadPipes();

    /**
     * Cleanup hooks for buildDone()
     */
    virtual void cleanupHookFinally();
    virtual void cleanupPreChildKill();
    virtual void cleanupPostChildKill();
    virtual bool cleanupDecideWhetherDiskFull();
    virtual void cleanupPostOutputsRegisteredModeCheck();
    virtual void cleanupPostOutputsRegisteredModeNonCheck();

protected:
    kj::TimePoint lastChildActivity = kj::minValue;

    kj::Promise<Outcome<void, Finished>> handleChildOutput() noexcept;
    kj::Promise<Outcome<void, Finished>>
    handleChildStreams(InputStream & builderIn, InputStream * hookIn) noexcept;
    kj::Promise<Outcome<void, Finished>> handleBuilderOutput(InputStream & in) noexcept;
    kj::Promise<Outcome<void, Finished>> handleHookOutput(InputStream & in) noexcept;
    kj::Promise<Outcome<void, Finished>> monitorForSilence() noexcept;
    Finished tooMuchLogs();
    void flushLine();

public:
    /**
     * Wrappers around the corresponding Store methods that first consult the
     * derivation.  This is currently needed because when there is no drv file
     * there also is no DB entry.
     */
    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap();
    OutputPathMap queryDerivationOutputMap();

    /**
     * Update 'initialOutputs' to determine the current status of the
     * outputs of the derivation. Also returns a Boolean denoting
     * whether all outputs are valid and non-corrupt, and a
     * 'SingleDrvOutputs' structure containing the valid outputs.
     */
    std::pair<bool, SingleDrvOutputs> checkPathValidity();

    /**
     * Aborts if any output is not valid or corrupt, and otherwise
     * returns a 'SingleDrvOutputs' structure containing all outputs.
     */
    SingleDrvOutputs assertPathValidity();

    /**
     * Forcibly kill the child process, if any.
     */
    virtual void killChild();

    kj::Promise<Result<WorkResult>> repairClosure() noexcept;

    void started();

    Finished done(
        BuildResult::Status status,
        SingleDrvOutputs builtOutputs = {},
        std::optional<Error> ex = {});

    void waiteeDone(GoalPtr waitee) override;

    virtual bool respectsTimeouts()
    {
        return false;
    }

    StorePathSet exportReferences(const StorePathSet & storePaths);

    JobCategory jobCategory() const override {
        return JobCategory::Build;
    };
};

MakeError(NotDeterministic, BuildError);

}
