.PHONY: all build clean test install deps help run-server run-client e2e e2e-quick

# 内存分配器选择
ifndef UVRPC_ALLOCATOR
UVRPC_ALLOCATOR=mimalloc
endif

ifeq ($(UVRPC_ALLOCATOR),mimalloc)
	CMAKE_FLAGS += -DUVRPC_USE_MIMALLOC=ON
	ALLOCATOR_MSG="mimalloc (高性能)"
else ifeq ($(UVRPC_ALLOCATOR),system)
	CMAKE_FLAGS += -DUVRPC_USE_SYSTEM_ALLOCATOR=ON
	ALLOCATOR_MSG="系统 malloc/free"
else
	CMAKE_FLAGS += -DUVRPC_USE_CUSTOM_ALLOCATOR=ON
	ALLOCATOR_MSG="自定义"
endif

# 默认目标
all: build
	@echo "✓ 使用分配器: $(ALLOCATOR_MSG)"

# 构建项目
build:
	@echo "Building uvrpc with $(ALLOCATOR_MSG)..."
	@mkdir -p build
	@cd build && cmake .. $(CMAKE_FLAGS) && make -j$$(nproc)

# 清理构建产物
clean:
	@echo "Cleaning build artifacts..."
	rm -rf build
	rm -f deps/libuv/build
	rm -f deps/uvzmq/build
	rm -f deps/uvzmq/third_party/zmq/build

# 安装依赖
deps:
	@echo "Installing dependencies..."
	git submodule update --init --recursive

# 构建依赖（仅构建，不编译项目）
deps-build:
	@echo "Building dependencies..."
	@./build/deps/build_deps.sh || (echo "Building deps manually..." && \
		cd deps/libuv && if [ ! -d "build" ]; then mkdir build; fi && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && make -j$$(nproc) && \
		cd ../../uvzmq && if [ ! -d "build" ]; then mkdir build; fi && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DUVZMQ_BUILD_TESTS=OFF -DUVZMQ_BUILD_BENCHMARKS=OFF -DUVZMQ_BUILD_EXAMPLES=OFF && make -j$$(nproc) && \
		cd ../third_party/zmq && if [ ! -d "build" ]; then mkdir build; fi && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_CURVE=OFF -DENABLE_DRAFTS=OFF -DWITH_PERF_TOOL=OFF -DBUILD_SHARED_LIBS=OFF -DZMQ_BUILD_TESTS=OFF && make -j$$(nproc))

# 运行测试
test:
	@echo "Running tests..."
	@cd build && ctest --output-on-failure || echo "No tests configured"

# 安装到系统
install:
	@echo "Installing uvrpc..."
	cd build && make install

# 运行 echo 服务器
run-server:
	@echo "Starting echo server..."
	@./build/echo_server tcp://127.0.0.1:5555

# 运行 echo 客户端
run-client:
	@echo "Starting echo client..."
	@./build/echo_client tcp://127.0.0.1:5555 "Hello, uvrpc!"

# 端到端测试（完整测试）
e2e:
	@echo "Running E2E test..."
	@echo "Starting server in background..."
	@timeout 10 ./build/echo_server tcp://127.0.0.1:5555 2>&1 & \
	SERVER_PID=$$!; \
	sleep 2; \
	echo "Running client..."; \
	timeout 5 ./build/echo_client tcp://127.0.0.1:5555 "E2E Test Message" 2>&1; \
	CLIENT_EXIT=$$?; \
	wait $$SERVER_PID 2>/dev/null || true; \
	if [ $$CLIENT_EXIT -eq 0 ]; then \
		echo "✓ E2E test passed"; \
	else \
		echo "✗ E2E test failed (exit code: $$CLIENT_EXIT)"; \
		exit 1; \
	fi

# 快速 E2E 测试（单次请求）
e2e-quick:
	@echo "Running quick E2E test..."
	@timeout 5 ./build/echo_server tcp://127.0.0.1:5556 2>&1 & \
	SERVER_PID=$$!; \
	sleep 1; \
	timeout 2 ./build/echo_client tcp://127.0.0.1:5556 "Quick Test" 2>&1 | grep -q "Echo:" && \
	echo "✓ Quick E2E test passed" || \
	(echo "✗ Quick E2E test failed"; wait $$SERVER_PID 2>/dev/null || true; exit 1); \
	wait $$SERVER_PID 2>/dev/null || true

# 性能测试
perf:
	@echo "Running performance test..."
	@./build/perf_test || echo "Performance test not built"

# 格式化代码
fmt:
	@echo "Formatting code..."
	@which clang-format > /dev/null && clang-format -i src/*.c include/*.h examples/*.c || echo "clang-format not found"

# 代码检查
check:
	@echo "Running code checks..."
	@which cppcheck > /dev/null && cppcheck --enable=all --suppress=missingIncludeSystem src/ include/ || echo "cppcheck not found"

# 生成文档
docs:
	@echo "Generating documentation..."
	@which doxygen > /dev/null && doxygen Doxyfile || echo "doxygen not found"

# 显示帮助信息
help:
	@echo "uvrpc Makefile - 类似 package.json 的 scripts 功能"
	@echo ""
	@echo "可用命令:"
	@echo "  make              - 构建项目（默认）"
	@echo "  make build        - 构建项目"
	@echo "  make clean        - 清理构建产物"
	@echo "  make deps         - 安装依赖（git submodule）"
	@echo "  make deps-build   - 构建依赖库"
	@echo "  make test         - 运行单元测试"
	@echo "  make e2e          - 运行端到端测试"
	@echo "  make e2e-quick    - 运行快速 E2E 测试"
	@echo "  make install      - 安装到系统"
	@echo "  make run-server   - 运行 echo 服务器"
	@echo "  make run-client   - 运行 echo 客户端"
	@echo "  make perf         - 运行性能测试"
	@echo "  make fmt          - 格式化代码"
	@echo "  make check        - 代码检查"
	@echo "  make docs         - 生成文档"
	@echo "  make help         - 显示此帮助信息"
	@echo ""
	@echo "内存分配器选项:"
	@echo "  UVRPC_ALLOCATOR=mimalloc  - 使用 mimalloc（默认）"
	@echo "  UVRPC_ALLOCATOR=system    - 使用系统 malloc/free"
	@echo "  UVRPC_ALLOCATOR=custom    - 使用自定义分配器"
	@echo ""
	@echo "示例:"
	@echo "  make                      # 使用 mimalloc 构建"
	@echo "  make UVRPC_ALLOCATOR=system # 使用系统分配器"
	@echo "  make UVRPC_ALLOCATOR=custom # 使用自定义分配器"
	@echo "  make e2e                   # 运行完整 E2E 测试"
	@echo "  make e2e-quick             # 运行快速 E2E 测试"
	@echo "  make run-server            # 启动服务器"
	@echo "  make run-client            # 启动客户端"