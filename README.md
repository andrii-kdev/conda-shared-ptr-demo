# Conda Package (shared-ptr-demo)
This package provides just a basic test implementation of shared ptr with a CMake-based build system and external dependencies (boost).
The package is currently built and tested for **Linux-64** only.

## Prerequisites
To build and run this project, you need to have **Conda** installed (Miniconda or Anaconda). 
Additionally, ensure you have `conda-build` installed in your `base` environment:

## 1. Build the package.
```bash
# To create local directory for build result:
mkdir ./artifacts
```
```bash
# To build shared-ptr-demo package run:
conda build conda-recipe/ --output-folder ./artifacts/
```

## 2. Install and Test.
```bash
# To create your own conda environment:
conda create -n my-test-env
```
```bash
# To activate your conda environment:
conda activate my-test-env
```

```bash
# To install package:
conda install shared-ptr-demo -c ./artifacts
```
```bash
# To run:
shared-ptr-demo
```

## 3. Cleanup
```bash
# To clean and remove your test environment:
conda deactivate
conda remove -n my-test-env --all
```
```bash
# To remove build artifacts:
rm -rf ./artifacts
```


## Quick Install.
```bash
# Just install pre-built package from public channel (uploaded to Anaconda.org).
# You may also create/remove your own conda test env as described in steps above.
conda install -c <your_username> shared-ptr-demo
```
