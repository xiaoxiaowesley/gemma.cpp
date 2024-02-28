

# cmake -DCMAKE_build/ios_TYPE=Debug \
#    -DCMAKE_TOOLCHAIN_FILE=./toolchains/ios.toolchain.cmake \
#    -DPLATFORM=SIMULATOR64 \
#    -DDEPLOYMENT_TARGET=9.0 \
#    -DIS_IOS=TRUE \
#    -DENABLE_BITCODE=FALSE -G "Unix Makefiles" -B ./build/ios-simlator/cmake-build/ios-ios-x64 -S .

# rm -rf build/ios/*

# simlator
cmake -DCMAKE_TOOLCHAIN_FILE=./toolchains/ios.toolchain.cmake \
    -DPLATFORM=SIMULATOR64 \
    -DDEPLOYMENT_TARGET=13.0 \
    -B ./build/ios/simlator -S  . -G "Unix Makefiles" 

# cmake -DCMAKE_TOOLCHAIN_FILE=./toolchains/ios.toolchain.cmake \
#     -DPLATFORM=OS64 \
#     -DDEPLOYMENT_TARGET=13.0 \
#     -B ./build/ios/arm64 -S . -G "Unix Makefiles"

# -DIOS_PLATFORM=OS64
cmake --build  ./build/ios/simlator --target libgemma -- -j 12
# cmake --build  ./build/ios/arm64 --target libgemma -- -j 12

# xcodebuild -create-xcframework  -framework build/ios/arm64/libgemma.framework  -framework build/ios/simlator/libgemma.framework -output ./build/ios/gemma.xcframework 