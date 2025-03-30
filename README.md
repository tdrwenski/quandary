# Quandary - Optimal control for open and closed quantum systems
Quandary implements an optimization solver for open and closed optimal quantum control. The underlying quantum dynamics model open or closed quantum systems, using either Schroedinger's equation for a state vector (closed), or Lindblad master equation for a density matrix (open). The control problem aims to find control pulses that drive the system to a desired target, such as a target unitary solution operator or to a predefined target state. Quandary targets deployment on High-Performance Computing platforms, offering various levels for parallelization using the message passing paradigm. 

It is advised to look at the user guide in `doc/`, describing the underlying mathematical models, their implementation and usage in Quandary. 

Feel free to reach out to Stefanie Guenther [guenther5@llnl.gov] for any question you may have. 

## Dependencies
This project relies on Petsc [https://petsc.org/release/] to handle (parallel) linear algebra. Optionally Slepsc [https://slepc.upv.es] can be used to solve some eigenvalue problems if desired (e.g. for the Hessian...)

### PETSc
* Download and install PETSc from [https://petsc.org/release/]
* On MacOS, you can also use: `brew install petsc`
* Make sure PETSc's pkg-config files are discoverable:
  ```
  export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/path/to/petsc/lib/pkgconfig
  ```

### SLEPc (Optional)
* If you want to use SLEPc for eigenvalue problems, install from [https://slepc.upv.es]
* Make sure SLEPc's pkg-config files are discoverable:
  ```
  export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/path/to/slepc/lib/pkgconfig
  ```

### PETSc/SLEPc on LLNL's LC
PETSc is already installed on LLNL LC machines. Load the appropriate modules:
```
module load petsc
module load openmpi
```

## Build and Installation

### Installing with Spack (Recommended)

Quandary can be installed using the [Spack](https://spack.io/) package manager, which automatically handles all dependencies:

1. If you don't have Spack installed, follow the [Spack installation instructions](https://spack.io/downloads/):
   ```
   git clone -c feature.manyFiles=true https://github.com/spack/spack.git
   . spack/share/spack/setup-env.sh
   ```

2. Add the Quandary repository to Spack:
   ```
   mkdir -p ~/spack-repos/quandary/packages/quandary
   cp /path/to/quandary/package.py ~/spack-repos/quandary/packages/quandary/
   echo "repo:" > ~/spack-repos/quandary/repo.yaml
   echo "  namespace: quandary_repo" >> ~/spack-repos/quandary/repo.yaml
   spack repo add ~/spack-repos/quandary
   ```

3. Install Quandary with Spack:
   ```
   spack install quandary
   ```

4. To install with SLEPc support:
   ```
   spack install quandary+slepc
   ```

5. Load Quandary in your environment:
   ```
   spack load quandary
   ```

#### Development Setup with Spack

To set up a development environment for Quandary using Spack:

1. Create a `spack.yaml` file in your Quandary directory:
   ```yaml
   spack:
     specs:
     - quandary@develop
     
     view: true
     
     concretizer:
       unify: true
       
     develop:
       quandary:
         spec: quandary@=develop
         path: .
   ```

2. Activate the environment:
   ```
   cd /path/to/quandary
   spack env activate .
   spack concretize -f
   spack install
   ```

This approach allows you to work on the Quandary code while Spack manages the dependencies.

### Building with CMake

Quandary uses CMake with the BLT build system for dependency management.

1. Clone the repository and update the submodules:
   ```
   git clone <repository-url>
   cd quandary
   git submodule update --init
   ```

2. Create a build directory and configure:
   ```
   mkdir build && cd build
   cmake ..
   ```

3. Build the project:
   ```
   cmake --build . -j
   ```

4. Optionally install:
   ```
   cmake --install .
   ```

#### CMake Configuration Options

* `-DWITH_SLEPC=ON`: Enable SLEPc for eigenvalue problems
* `-DSANITY_CHECK=ON`: Enable sanity checks
* `-DCMAKE_INSTALL_PREFIX=/path/to/install`: Set installation directory
* `-DPETSC_DIR=/path/to/petsc`: Specify custom PETSc installation path (if not found via pkg-config)
* `-DSLEPC_DIR=/path/to/slepc`: Specify custom SLEPc installation path (if not found via pkg-config)

### Building with Make (Legacy)

The original Makefile build system is still available:

1. Adapt the beginning of the 'Makefile' to set the path to your Petsc (and possibly Slepsc) installation, if not exported:
   ```
   export PETSC_DIR=/path/to/petsc
   export PETSC_ARCH=arch-linux-c-debug  # If required
   ```

2. Clean and build:
   ```
   make cleanup
   make -j quandary
   ```

It is advised to add Quandary to your `PATH`:
```
export PATH=$PATH:/path/to/quandary/
```

## Running
The code builds into the executable `quandary`. It takes one argument: the name of the test-case's configuration file. The file `config_template.cfg` lists all possible configuration options.

Examples:
```
./quandary config_template.cfg
mpirun -np 4 ./quandary config_template.cfg --quiet
```

**Python Interface:** To run Quandary from within a Python environment, make sure you have numpy and matplotlib installed, and add Quandary to your Python path:
```
export PYTHONPATH=$PYTHONPATH:/path/to/quandary/
```
See the `examples/pythoninterface/` directory for examples.

## Community and Contributing

Quandary is an open source project that is under heavy development. Contributions in all forms are very welcome, and can be anything from new features to bugfixes, documentation, or even discussions. Contributing is easy, work on your branch, create a pull request to master when you're good to go and the regression tests in 'tests/' pass.

## Publications
* S. Guenther, N.A. Petersson, J.L. DuBois: "Quantum Optimal Control for Pure-State Preparation Using One Initial State", AVS Quantum Science, vol. 3, arXiv preprint <https://arxiv.org/abs/2106.09148> (2021)
* S. Guenther, N.A. Petersson, J.L. DuBois: "Quandary: An open-source C++ package for High-Performance Optimal Control of Open Quantum Systems", submitted to IEEE Supercomputing 2021, arXiv preprint <https://arxiv.org/abs/2110.10310> (2021)

## License

Quandary is distributed under the terms of the MIT license. All new contributions must be made under this license. See LICENSE, and NOTICE, for details. 

SPDX-License-Identifier: MIT