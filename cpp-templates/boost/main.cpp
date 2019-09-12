#include <iostream>
#include <string>
#include <vector>
#include <boost/algorithm/string/join.hpp>

using namespace std;

auto main() -> int {
  auto res = boost::algorithm::join(vector<string>({"one", "two"}), ", ");
  cout << res << endl;
  return 0;
}
