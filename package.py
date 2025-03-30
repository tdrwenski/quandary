# Copyright 2013-2024 Lawrence Livermore National Security, LLC and other
# Spack Project Developers. See the top-level COPYRIGHT file for details.
#
# SPDX-License-Identifier: (Apache-2.0 OR MIT)

from spack.package import *


class Quandary(CMakePackage):
    """Quandary - Optimal control for open and closed quantum systems.

    Quandary implements an optimization solver for open and closed optimal 
    quantum control. The underlying quantum dynamics model open or closed 
    quantum systems, using either Schroedinger's equation for a state vector
    (closed), or Lindblad master equation for a density matrix (open). The 
    control problem aims to find control pulses that drive the system to a 
    desired target, such as a target unitary solution operator or to a 
    predefined target state. Quandary targets deployment on 
    High-Performance Computing platforms, offering various levels for 
    parallelization using the message passing paradigm.
    """

    homepage = "https://github.com/LLNL/quandary"
    # url = "https://github.com/LLNL/quandary/archive/v1.0.0.tar.gz"
    git = "https://github.com/LLNL/quandary.git"

    maintainers("guenther5")

    version("main", branch="main")
    version("1.0.0", tag="v1.0.0")

    variant("slepc", default=False, description="Enable SLEPc for eigenvalue problems")
    variant("sanity", default=False, description="Enable sanity checks")

    depends_on("cmake@3.14:", type="build")
    depends_on("mpi")
    depends_on("petsc")
    depends_on("slepc", when="+slepc")
    depends_on("pkgconfig", type="build")

    def cmake_args(self):
        args = [
            self.define_from_variant("WITH_SLEPC", "slepc"),
            self.define_from_variant("SANITY_CHECK", "sanity"),
            self.define("PETSC_DIR", self.spec["petsc"].prefix),
            self.define("SLEPC_DIR", self.spec["slepc"].prefix, when="+slepc"),
        ]
        return args

    def setup_run_environment(self, env):
        # Set up environment for python interface
        env.prepend_path("PYTHONPATH", self.prefix.lib.python)
        env.set("QUANDARY_DIR", self.prefix)