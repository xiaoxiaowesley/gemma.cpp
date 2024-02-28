

# cmake -DCMAKE_BUILD_TYPE=Debug \
#    -DCMAKE_TOOLCHAIN_FILE=./toolchains/ios.toolchain.cmake \
#    -DPLATFORM=SIMULATOR64 \
#    -DDEPLOYMENT_TARGET=9.0 \
#    -DIS_IOS=TRUE \
#    -DENABLE_BITCODE=FALSE -G "Unix Makefiles" -B ./build/mac/cmake-build-ios-x64 -S .

rm -rf build/mac/*

cmake -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"  -B ./build/mac

cmake --build ./build/mac --target libgemma -- -j 12

xcodebuild -create-xcframework -framework ./build/mac/libgemma.framework -output ./build/mac/gemma.xcframework 