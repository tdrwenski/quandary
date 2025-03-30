# LLM test notes

Goal: use claude to create CMake files and spack package

Summary:
For CMake, it worked reasonably well but I think its better to give step by step instructions instead of one prompt with several instructions (first time it did not actually add BLT as a submodule even though I had said that). Resulting CMake file seems okay and it was able to test it works.

For spack, it worked okay at making the package.py, but was a bit tripped up trying to add an environment with quandary in develop mode, it had the syntax for that slightly wrong and went the wrong route trying to fix it until I told it what to do. It was able to test it worked. But in the end, there are these nuances that it probably misses so it is nice to get you 90% there but you still need to look into all the details to get it right.


## CMake (~20 min, ~5 prompts)

- "Use CMake to build this project instead of Makefile. Use the BLT library to help with this by adding it as a git submodule. Also automatically find dependencies like Petsc using CMake's find_package instead of requiring environment variables to find them. If Petsc is in a non-default location then it should be possible to use a CMake variable to specify where it is. Update the README with build instructions."
- Starts making CMake files, including a whole lot of logic for finding petsc
- "Petsc uses pkgconfig so it shouldn't be necessary to write your own macro to find it"
-> starts making readme instructions but hasn;t used BLT at all
- "Clone BLT and add it as a submodule. Use in CMake build"
-> does clone and add BLT functionality now. Updates README too
- "Lets also test that the CMake commands in the readme work correctly"
-> fails to compile due to MPI, updates CMake, then does compile and install binary
- "commit these changes"
- " Can you also run the tests to make sure the binary is working correctly?"
-> runs several tests and they fail due to no numpy in env but it can run the test config successfully

- "can you compare the cmake files you made to the ones on the main branch?"

```
Main Differences Between Our CMake Implementation and the Existing One

1. Overall Structure:
    - Existing: Split into two files (main CMakeLists.txt and src/CMakeLists.txt)
    - Ours: All in a single CMakeLists.txt file
2. Library Organization:
    - Existing: Creates a separate library target (quandary_lib) and links the executable to it
    - Ours: Builds the executable directly from all sources
3. PETSc Configuration:
    - Existing: Uses pkg_search_module for PETSc
    - Ours: Uses pkg_check_modules and adds explicit support for custom PETSc locations via the PETSC_DIR variable
4. SLEPc Configuration:
    - Existing: Links SLEPc to the executable only
    - Ours: More flexible approach with SLEPc linkage using generator expressions
5. Dependencies:
    - Existing: Explicitly links against m, blas, and lapack
    - Ours: Relies on PETSc's dependency resolution
6. Output Location:
    - Existing: Sets the executable output to the project root
    - Ours: Uses CMake default (bin directory)
7. Additional Installation:
    - Existing: Only installs the executable
    - Ours: Also installs the configuration template and Python interface
8. BLT Configuration:
    - Existing: Minimal configuration
    - Ours: More explicit with GTEST, GMOCK, and BENCHMARKS settings
9. Error Handling:
    - Existing: No explicit error handling for missing BLT
    - Ours: Checks if BLT is present and issues an error if not

Strengths and Weaknesses

Existing Implementation Strengths:
- Separation of concerns with src/CMakeLists.txt
- Library approach allows for better component reuse
- Explicit linking to math libraries might be more robust

Our Implementation Strengths:
- More comprehensive with additional installation targets
- Explicit error checking for submodules
- Custom PETSc/SLEPc location support
- Cleaner generator expressions for optional components
- Simpler structure with a single file

Recommendations for Combining the Best of Both:
1. Keep the library-based approach from the existing implementation
2. Incorporate our custom PETSc/SLEPc path handling
3. Add our additional installation targets
4. Keep our explicit submodule error checking
5. Use our generator expressions for cleaner conditional compilation

Overall, both implementations are functional, but they take different approaches to the same problem. The existing implementation has a better architecture with the library
separation, while our implementation has more comprehensive dependency handling and installation targets.
```

## Spack (~30 min, ~15 prompts)

- "Can you create a spack package for this project so that it can be installed with `spack install`?"
-> created package.py
- "Can you make a local spack environment with quandary in it and test that it works?"
-> starts adding spack.yml, repo.yml
- "Can you add quandary to the environment as a development package to build local version from source?"
-> does this but still adds current branch to list of versions in package.py so doesnt seem to completely understand to the develop mode
- "The version in the spack.yml should have to be "test-llm" though, since it is using the develop version. Let's call it develop instead and remove "test-llm" from versions in the package.py "
-> updates spack.yaml but still tries to keep branch version in package.py
- "remove version test-llm entirely from package.py"
-> still tries to add `version("develop")` to package.py
- "this should not be needed"
-> removes and starts to retest by deactivate/ reactivating spack env
- "this is not exactly right, now it is uses quandary@1.0.0, we want it to use quandary@develop"
-> still struggling to
- "Please only add the develop version to the spack.yaml by adding `quandary@develop` to spec. That tells spack to build local source code"
-> again tries to add develop version to package.py
- "this shouldnt be needed. Also path through be relative path not absolute path in spack.yaml"
-> updates the path but is still 
- "can you try to do `spack concretize -f` when testing spack changes?"
-> still does not work, concretize error
- "I believe that in the develop section in the spack.yaml you need to have `quandary@=develop` isntead of `quandary@develop`"
-> does that and tests that it works
- "Can you add something to the readme about spack?"
-> tries to add package.py again under a different path when updating readme?
- "isn't this already present in the base of this repo?"
-> updates path in readme instead of duplicating file
- "commit these changes"

- copied original package.py here for comparison. "Can you compare the spack package.py on this branch to package-orig.py?"

```
⏺ Here's a comparison between package.py and package-orig.py:

  1. Base class changes:
    - package.py: Uses CMakePackage as the base class
    - package-orig.py: Uses CachedCMakePackage, CudaPackage, ROCmPackage (multiple inheritance)
  2. Description/Documentation:
    - package.py: Has a more detailed description of Quandary
    - package-orig.py: Has a shorter description "Optimal control for open quantum systems"
  3. Maintainers:
    - package.py: Lists "guenther5"
    - package-orig.py: Lists "steffi7574", "tdrwenski"
  4. Version:
    - package.py: Defines version "main" and "1.0.0"
    - package-orig.py: Only defines version "main"
  5. Dependencies:
    - package.py: Simpler dependencies (cmake, mpi, petsc, slepc when +slepc, pkgconfig)
    - package-orig.py: More complex dependencies including conditional dependencies for CUDA and ROCm
  6. Variants:
    - package.py: Has "slepc" and "sanity" variants
    - package-orig.py: Has "slepc", "debug", and "test" variants
  7. Build Configuration:
    - package.py: Uses newer Spack syntax with self.define_from_variant and includes PETSC_DIR and SLEPC_DIR definitions
    - package-orig.py: Uses older syntax and has an additional initconfig_package_entries method
  8. Environment Setup:
    - package.py: Includes setup_run_environment for Python interface
    - package-orig.py: Does not include this setup
  9. GPU Support:
    - package.py: No explicit GPU support
    - package-orig.py: Has support for CUDA and ROCm

  The new package.py appears to be a simplified version that focuses on the core functionality without the GPU acceleration options, and adds environment setup for the Python
  interface.

```

Some differences: 
- should MPI by a build/link dependency or a run dependendency? not sure if this is correct in either version
- Claude did not add BLT as a dependency
- Claude did not use CachedCMakePackage
- Claude uses `define_from_variant`, maybe that is nicer