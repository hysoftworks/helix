#include "lix/libcmd/editor-for.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/source-path.hh"
#include "lix/libutil/strings.hh"

namespace nix {

Strings editorFor(const SourcePath & file, uint32_t line)
{
    auto maybe_editor = getEnv("EDITOR");
    if (!maybe_editor) {
        throw Error("the environment variable EDITOR is empty");
    }
    std::string editor = *maybe_editor;
    auto args = tokenizeString<Strings>(editor);
    if (line > 0 && (
        editor.find("emacs") != std::string::npos ||
        editor.find("nano") != std::string::npos ||
        editor.find("vim") != std::string::npos ||
        editor.find("kak") != std::string::npos))
        args.push_back(fmt("+%d", line));
    args.push_back(file.canonical().abs());
    return args;
}

Strings editorFor(const SourcePath & file)
{
    auto editor = getEnv("EDITOR");
    if (!editor) {
        throw Error("the environment variable EDITOR is empty");
    }
    auto args = tokenizeString<Strings>(*editor);
    args.push_back(file.canonical().abs());
    return args;
}

}
