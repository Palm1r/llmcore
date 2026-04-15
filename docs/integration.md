# Integration

## FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    LLMQore
    GIT_REPOSITORY https://github.com/palm1r/llmqore.git
    GIT_TAG v0.3.0
)
FetchContent_MakeAvailable(LLMQore)

target_link_libraries(YourApp PRIVATE LLMQore::LLMQore)
```

## Installed

```bash
cmake -B build -DLLMQORE_INSTALL=ON
cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(LLMQore REQUIRED)
target_link_libraries(YourApp PRIVATE LLMQore::LLMQore)
```

## Building from source

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
cmake --build build
```

Tests and examples:

```bash
cmake -B build -DLLMQORE_BUILD_TESTS=ON -DLLMQORE_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```