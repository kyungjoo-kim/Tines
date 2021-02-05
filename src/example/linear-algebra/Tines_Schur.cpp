#include "Tines.hpp"

int main(int argc, char **argv) {
  Kokkos::initialize(argc, argv);
  {
    using real_type = double;

    using host_exec_space = Kokkos::DefaultHostExecutionSpace;
    using host_memory_space = Kokkos::HostSpace;
    using host_device_type = Kokkos::Device<host_exec_space, host_memory_space>;

    using ats = Tines::ats<real_type>;
    using Side = Tines::Side;
    using Trans = Tines::Trans;
    using Uplo = Tines::Uplo;
    using Diag = Tines::Diag;

    std::string filename;
    int m = 54;
    Kokkos::View<real_type **, Kokkos::LayoutRight, host_device_type> A("A", m,
                                                                        m);
    if (argc == 2) {
      filename = argv[1];
      Tines::readMatrix(filename, A);
      m = A.extent(0);
    }
    Kokkos::View<real_type **, Kokkos::LayoutRight, host_device_type> B("B", m,
                                                                        m);
    Kokkos::View<real_type **, Kokkos::LayoutRight, host_device_type> Q("Q", m,
                                                                        m);
    Kokkos::View<real_type **, Kokkos::LayoutRight, host_device_type> QQ("QQ",
                                                                         m, m);
    Kokkos::View<real_type **, Kokkos::LayoutRight, host_device_type> eig("eig",
                                                                          m, 2);
    Kokkos::View<int *, Kokkos::LayoutRight, host_device_type> b("b", m);

    const real_type one(1), zero(0);
    const auto member = Tines::HostSerialTeamMember();

    if (filename.empty()) { // && m != 3 && m != 4) {
      Kokkos::Random_XorShift64_Pool<host_device_type> random(13718);
      Kokkos::fill_random(A, random, real_type(1.0));
    }
    // if (m == 3) { A(0,2) = 1; A(1,0) = 1; A(2,1) = 1;}
    // if (m == 4) { const real_type h = ats::epsilon();
    //   A(1,0) = 1; A(0,1) = 1; A(3,2) = 1; A(2,3) = 1;
    //   A(2,1) = -h; A(1,2) = h;
    // }

    bool is_valid(false);
    Tines::CheckNanInf::invoke(member, A, is_valid);
    std::cout << "Random matrix created "
              << (is_valid ? "is valid" : "is NOT valid") << "\n\n";

    Tines::SetTriangularMatrix<Uplo::Lower>::invoke(member, 2, zero, A);
    Tines::SetIdentityMatrix::invoke(member, Q);
    Tines::Copy::invoke(member, A, B);
    Tines::showMatrix("A", A);

    /// A = Q T Q^H
    auto er = Kokkos::subview(eig, Kokkos::ALL(), 0);
    auto ei = Kokkos::subview(eig, Kokkos::ALL(), 1);
    int r_val = Tines::Schur::invoke(member, A, Q, er, ei, b);
    Tines::SetTriangularMatrix<Uplo::Lower>::invoke(member, 2, zero, A);
    if (r_val == 0) {
      Tines::showMatrix("A (after Schur)", A);
      Tines::showMatrix("Q (after Schur)", Q);
      /// QQ = Q Q'
      Tines::Gemm<Trans::NoTranspose, Trans::Transpose>::invoke(member, one, Q,
                                                                Q, zero, QQ);
      Tines::showMatrix("Q Q'", QQ);
      {
        real_type err(0);
        for (int i = 0; i < m; ++i)
          for (int j = 0; j < m; ++j) {
            const real_type diff = ats::abs(QQ(i, j) - (i == j ? one : zero));
            err += diff * diff;
          }
        const real_type rel_err = ats::sqrt(err / (m));

        const real_type margin = 100, threshold = ats::epsilon() * margin;
        if (rel_err < threshold) {
          std::cout << "PASS Schur Q Orthogonality " << rel_err << "\n\n";
        } else {
          std::cout << "FAIL Schur Q Orthogonality " << rel_err << "\n\n";
        }
      }

      /// B = A - QHQ^H
      real_type norm(0);
      {
        for (int i = 0; i < m; ++i)
          for (int j = 0; j < m; ++j) {
            const real_type val = ats::abs(B(i, j));
            norm += val * val;
          }
      }
      Tines::Gemm<Trans::NoTranspose, Trans::NoTranspose>::invoke(
        member, one, Q, A, zero, QQ);
      Tines::Gemm<Trans::NoTranspose, Trans::Transpose>::invoke(member, -one,
                                                                QQ, Q, one, B);
      Tines::showMatrix("B (A-QHQ^H)", B);
      {
        real_type err(0);
        for (int i = 0; i < m; ++i)
          for (int j = 0; j < m; ++j) {
            const real_type diff = ats::abs(B(i, j));
            err += diff * diff;
          }
        const real_type rel_err = ats::sqrt(err / norm);

        const real_type margin = 100 * m * m,
                        threshold = ats::epsilon() * margin;
        /// printf("threshold %e\n", threshold);
        if (rel_err < threshold) {
          std::cout << "PASS Schur " << rel_err << "\n\n";
        } else {
          std::cout << "FAIL Schur " << rel_err << "\n\n";
        }
      }
      Tines::showVector("blocks", b);
      Tines::showMatrix("eigen values", eig);
    } else {
      printf("Schur does not converge at a row %d\n", -r_val);
    }
  }
  Kokkos::finalize();
  return 0;
}