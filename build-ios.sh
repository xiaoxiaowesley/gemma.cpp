

cmake -DCMAKE_BUILD_TYPE=Debug \
   -DCMAKE_TOOLCHAIN_FILE=./toolchains/ios.toolchain.cmake \
   -DPLATFORM=SIMULATOR64 \
   -DDEPLOYMENT_TARGET=9.0 \
   -DIS_IOS=TRUE \
   -DENABLE_BITCODE=FALSE -G "Unix Makefiles" -B ./build/cmake-build-ios-x64 -S .