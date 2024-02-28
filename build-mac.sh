

# cmake -DCMAKE_BUILD_TYPE=Debug \
#    -DCMAKE_TOOLCHAIN_FILE=./toolchains/ios.toolchain.cmake \
#    -DPLATFORM=SIMULATOR64 \
#    -DDEPLOYMENT_TARGET=9.0 \
#    -DIS_IOS=TRUE \
#    -DENABLE_BITCODE=FALSE -G "Unix Makefiles" -B ./build/cmake-build-ios-x64 -S .

rm -rf build/*

# cmake -DCMAKE_OSX_ARCHITECTURES=arm64  -B ./build
cmake -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"  -B ./build
# cmake  -B ./build

cd build 
make -j 4 libgemma

# build xcfamework
xcodebuild -create-xcframework  -library ./build/libgemma.dylib  -output ./build/gemma.xcframework 