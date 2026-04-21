# create build folder
mkdir build
cd build

# CMAKE_ARGS contains important flags from Conda (compiler path, etc.)
# PREFIX - path to active conda environment
cmake ${CMAKE_ARGS} \
      -DCMAKE_INSTALL_PREFIX=$PREFIX \
      -DCMAKE_BUILD_TYPE=Release \
      ..

# build and install
make -j${CPU_COUNT}
make install
