#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "config.hh"

namespace nix {

struct CmdConfig : MultiCommand
{
    CmdConfig() : MultiCommand(CommandRegistry::getCommandsFor({"config"}))
    { }

    std::string description() override
    {
        return "manipulate the Lix configuration";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix config' requires a sub-command.");
        command->second->run();
    }
};

struct CmdConfigShow : Command, MixJSON
{
    std::optional<std::string> name;

    CmdConfigShow() {
        expectArgs({
            .label = {"name"},
            .optional = true,
            .handler = {&name},
        });
    }

    std::string description() override
    {
        return "show the Lix configuration or the value of a specific setting";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (name) {
            if (json) {
                throw UsageError("'--json' is not supported when specifying a setting name");
            }

            std::map<std::string, Config::SettingInfo> settings;
            globalConfig.getSettings(settings);
            auto setting = settings.find(*name);

            if (setting == settings.end()) {
                throw Error("could not find setting '%1%'", *name);
            } else {
                const auto & value = setting->second.value;
                logger->cout("%s", value);
            }

            return;
        }

        if (json) {
            // FIXME: use appropriate JSON types (bool, ints, etc).
            logger->cout("%s", globalConfig.toJSON().dump());
        } else {
            logger->cout("%s", globalConfig.toKeyValue());
        }
    }
};

void registerNixConfig()
{
    registerCommand<CmdConfig>("config");
    registerCommand2<CmdConfigShow>({"config", "show"});
}

}
