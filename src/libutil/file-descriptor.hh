#pragma once
///@file

#include "error.hh"

namespace nix {

struct Sink;
struct Source;

/**
 * Read a line from a file descriptor.
 */
std::string readLine(int fd);

/**
 * Write a line to a file descriptor.
 */
void writeLine(int fd, std::string s);

/**
 * Read the contents of a file into a string.
 */
std::string readFile(int fd);

/**
 * Wrappers arount read()/write() that read/write exactly the
 * requested number of bytes.
 */
void readFull(int fd, char * buf, size_t count);
void writeFull(int fd, std::string_view s, bool allowInterrupts = true);

/**
 * Read a file descriptor until EOF occurs.
 */
std::string drainFD(int fd, bool block = true, const size_t reserveSize=0);

void drainFD(int fd, Sink & sink, bool block = true);

class AutoCloseFD
{
    int fd;
public:
    AutoCloseFD();
    explicit AutoCloseFD(int fd);
    AutoCloseFD(const AutoCloseFD & fd) = delete;
    AutoCloseFD(AutoCloseFD&& fd);
    ~AutoCloseFD();
    AutoCloseFD& operator =(const AutoCloseFD & fd) = delete;
    AutoCloseFD& operator =(AutoCloseFD&& fd) noexcept(false);
    int get() const;
    explicit operator bool() const;
    int release();
    void close();
    void fsync();
    void reset() { *this = {}; }
};

class Pipe
{
public:
    AutoCloseFD readSide, writeSide;
    void create();
    void close();
};

/**
 * Close all file descriptors except those listed in the given set.
 * Good practice in child processes.
 */
void closeMostFDs(const std::set<int> & exceptions);

/**
 * Set the close-on-exec flag for the given file descriptor.
 */
void closeOnExec(int fd);

MakeError(EndOfFile, Error);

/**
 * Create a Unix domain socket.
 */
AutoCloseFD createUnixDomainSocket();

/**
 * Create a Unix domain socket in listen mode.
 */
AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode);
}
