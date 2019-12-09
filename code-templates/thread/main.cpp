#include <iostream>
#include <thread>

using namespace std;

auto main() -> int {

  thread tr([] () {
    cout << "Hello from thread" << endl;
  });

  cout << "Hello from main thread" << endl;

  tr.join();

  return 0;
}
