#include <Kokkos_Core.hpp>

int main(int argc, char** argv) {
    Kokkos::initialize(argc, argv);
    {
        // Allocate a 1-dimensional view of integers
        Kokkos::View<int*> v("v", 5);
        // Fill view with sequentially increasing values v=[0,1,2,3,4]
        Kokkos::parallel_for("fill", 5, KOKKOS_LAMBDA(int i) { v(i) = i; });
        // Compute accumulated sum of v's elements r=0+1+2+3+4
        int r;
        Kokkos::parallel_reduce(
            "accumulate", 5,
            KOKKOS_LAMBDA(int i, int& partial_r) { partial_r += v(i); }, r);
        // Check the result
        KOKKOS_ASSERT(r == 10);
    }
    Kokkos::printf("Goodbye World\n");
    Kokkos::finalize();
    return 0;
}
