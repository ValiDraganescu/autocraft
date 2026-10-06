# Builds AutocraftCore (../Source/AutocraftCore) and its tests with plain
# clang++, no Unreal, and runs them: `make -C unreal/core-tests`.
# `make -C unreal/core-tests FILTER=noise` runs only tests whose name
# contains "noise". Objects go to build/.

CXX := clang++
CORE := ../Source/AutocraftCore
REPO := $(abspath ../..)
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror -ffp-contract=off -fno-exceptions -fno-rtti \
	-I$(CORE)/Public -I$(CORE)/ThirdParty -Itests -DAC_REPO_ROOT='"$(REPO)"' -MMD -MP

# The tracking store (TrackingStore.cpp) speaks SQLite's C API; the tests link the
# system's libsqlite3 (the game links the engine's SQLiteCore instead).
LDLIBS := -lsqlite3

CORE_SRC := $(wildcard $(CORE)/Private/*.cpp)
TEST_SRC := $(wildcard tests/*.cpp)
CORE_OBJ := $(patsubst $(CORE)/Private/%.cpp,build/core/%.o,$(CORE_SRC))
TEST_OBJ := $(patsubst tests/%.cpp,build/tests/%.o,$(TEST_SRC))
BIN := build/core-tests

.PHONY: all run clean bench
all: run

run: $(BIN)
	./$(BIN) $(FILTER)

$(BIN): $(CORE_OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

build/core/%.o: $(CORE)/Private/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# `make bench`: build/simbench, the headless sim benchmark (bench/SimBench.cpp),
# with the core at -O3 in build/bench/.
BENCH_FLAGS := $(subst -O2,-O3,$(CXXFLAGS))
BENCH_OBJ := $(patsubst $(CORE)/Private/%.cpp,build/bench/core/%.o,$(CORE_SRC)) build/bench/SimBench.o

bench: build/simbench

build/simbench: $(BENCH_OBJ)
	$(CXX) $(BENCH_FLAGS) -o $@ $^ $(LDLIBS)

build/bench/core/%.o: $(CORE)/Private/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(BENCH_FLAGS) -c $< -o $@

build/bench/SimBench.o: bench/SimBench.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(BENCH_FLAGS) -c $< -o $@

clean:
	rm -rf build

-include $(CORE_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(BENCH_OBJ:.o=.d)
