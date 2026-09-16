#ifndef LIDL_CLI_HPP
#define LIDL_CLI_HPP

#include <iosfwd>

namespace lidl {

// The `lidl` tool, stream-injected so tests drive it in-process. Returns the exit code:
// 0 ok, 1 usage, 2 unreadable input, 3 parse error, 4 identity error, 5 validation errors.
int runCli(int argc, const char* const* argv, std::istream& in, std::ostream& out, std::ostream& err);

} // namespace lidl

#endif // LIDL_CLI_HPP
