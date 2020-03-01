#include "pch.h"

using namespace std;

auto main() -> int {
  auto res = boost::algorithm::join(vector<string>({"one", "two"}), ", ");
  cout << res << endl;
  return 0;
}
