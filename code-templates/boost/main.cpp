#include "pch.hpp"

auto main() -> int {
    using boost::algorithm::join;
    using std::string;
    using std::vector;

    auto const res = join(vector<string>({"one", "two"}), ", ");
    std::println("{}", res);
    return 0;
}
