#include "lidl/cli.hpp"

#include <iostream>

int main(int argc, char** argv)
{
    return lidl::runCli(argc, argv, std::cin, std::cout, std::cerr);
}
