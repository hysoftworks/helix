#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/types.hh"
#include "lix/libexpr/pos-idx.hh"

namespace nix {

struct DebugTrace;
struct Env;
struct Expr;
struct Value;

class EvalState;
template<class T>
class EvalErrorBuilder;

class EvalError : public Error
{
    template<class T>
    friend class EvalErrorBuilder;

    std::shared_ptr<const DebugTrace> frame;

public:
    EvalState & state;

    EvalError(EvalState & state, ErrorInfo && errorInfo)
        : Error(errorInfo)
        , state(state)
    {
    }

    template<typename... Args>
    explicit EvalError(EvalState & state, const std::string & formatString, const Args &... formatArgs)
        : Error(formatString, formatArgs...)
        , state(state)
    {
    }
};

MakeError(ParseError, Error);
MakeError(AssertionError, EvalError);
MakeError(ThrownError, AssertionError);
MakeError(Abort, EvalError);
MakeError(TypeError, EvalError);
MakeError(UndefinedVarError, EvalError);
MakeError(MissingArgumentError, EvalError);
MakeError(RestrictedPathError, Error);
MakeError(InfiniteRecursionError, EvalError);

/**
 * Represents an exception due to an invalid path; that is, it does not exist.
 * It corresponds to `!Store::validPath()`.
 */
struct InvalidPathError : public EvalError
{
public:
    Path path;
    InvalidPathError(EvalState & state, const Path & path)
        : EvalError(state, "path '%s' did not exist in the store during evaluation", path)
    {
    }
};

/**
 * `EvalErrorBuilder`s may only be constructed by `EvalState`. The `debugThrow`
 * method must be the final method in any such `EvalErrorBuilder` usage, and it
 * handles deleting the object.
 */
template<class T>
class EvalErrorBuilder final
{
    friend class EvalState;

    template<typename... Args>
    explicit EvalErrorBuilder(EvalState & state, const Args &... args)
        : error(T(state, args...))
    {
    }

public:
    T error;

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withExitStatus(unsigned int exitStatus);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & atPos(PosIdx pos);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & atPos(Value & value, PosIdx fallback = noPos);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withTrace(PosIdx pos, const std::string_view text);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withSuggestions(Suggestions & s);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withFrame(const Env & e, const Expr & ex);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & addTrace(PosIdx pos, HintFmt hint);

    template<typename... Args>
    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> &
    addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs);

    /**
     * Delete the `EvalErrorBuilder` and throw the underlying exception.
     */
    [[gnu::noinline, gnu::noreturn]] void debugThrow();
};

}
