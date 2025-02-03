def pytest_addoption(parser):
    parser.addoption(
        "--exact",
        action="store_true",
        default=False,
        help="Use exact comparison for floating point numbers"
    )

    parser.addoption(
        "--mpi-exec",
        action="store",
        default="mpirun",
        help="Path to the MPI executable (e.g., mpirun or srun)"
    )
