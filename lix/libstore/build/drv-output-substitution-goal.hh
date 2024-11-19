#pragma once
///@file

#include "lix/libutil/notifying-counter.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/build/goal.hh"
#include "lix/libstore/realisation.hh"
#include <future>

namespace nix {

class Worker;

/**
 * Substitution of a derivation output.
 * This is done in three steps:
 * 1. Fetch the output info from a substituter
 * 2. Substitute the corresponding output path
 * 3. Register the output info
 */
class DrvOutputSubstitutionGoal : public Goal {

    /**
     * The drv output we're trying to substitute
     */
    DrvOutput id;

    /**
     * The realisation corresponding to the given output id.
     * Will be filled once we can get it.
     */
    std::shared_ptr<const Realisation> outputInfo;

    /**
     * The remaining substituters.
     */
    std::list<ref<Store>> subs;

    /**
     * The current substituter.
     */
    std::shared_ptr<Store> sub;

    NotifyingCounter<uint64_t>::Bump maintainRunningSubstitutions;

    struct DownloadState
    {
        kj::Own<kj::CrossThreadPromiseFulfiller<void>> outPipe;
        std::future<std::shared_ptr<const Realisation>> result;
    };

    std::shared_ptr<DownloadState> downloadState;

    /**
     * Whether a substituter failed.
     */
    bool substituterFailed = false;

public:
    DrvOutputSubstitutionGoal(
        const DrvOutput & id,
        Worker & worker,
        bool isDependency,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt
    );

    kj::Promise<Result<WorkResult>> tryNext() noexcept;
    kj::Promise<Result<WorkResult>> realisationFetched() noexcept;
    kj::Promise<Result<WorkResult>> outPathValid() noexcept;
    kj::Promise<Result<WorkResult>> finished() noexcept;

    kj::Promise<Result<WorkResult>> workImpl() noexcept override;

    JobCategory jobCategory() const override {
        return JobCategory::Substitution;
    };
};

}
