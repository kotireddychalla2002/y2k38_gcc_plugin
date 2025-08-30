# Simple Makefile for a GCC plugin

# Try to automatically find the GCC version and plugin directory
GCC_VERSION := $(shell gcc -dumpversion)
GCC_PLUGIN_DIR := $(shell gcc -print-file-name=plugin)
CXX := g++

# Compiler flags for the plugin
CXXFLAGS := -I$(GCC_PLUGIN_DIR)/include -std=c++11 -fPIC -shared -Wall -Wextra

# Add a debug flag if requested
ifeq ($(DEBUG), 1)
    CXXFLAGS += -DDEBUG_PLUGIN
endif

# Target shared object
PLUGIN_SO := narrowing_cast_plugin.so
# Source file for the plugin
PLUGIN_SRC := narrowing_cast_plugin.cc
# Test application source and binary
TEST_SRC := test.cc
TEST_APP := test_app

# Default target: build the plugin
all: $(PLUGIN_SO)

# Rule to build the plugin
$(PLUGIN_SO): $(PLUGIN_SRC)
	@echo "Compiling plugin for GCC version $(GCC_VERSION)..."
	$(CXX) $(CXXFLAGS) $(PLUGIN_SRC) -o $(PLUGIN_SO)

# Rule to run the plugin on the test file
test: $(PLUGIN_SO) $(TEST_SRC)
	@echo "Running plugin on $(TEST_SRC)..."
	$(CXX) -std=c++11 -fplugin=./$(PLUGIN_SO) $(TEST_SRC) -o $(TEST_APP)

# Rule to clean up build artifacts
clean:
	@echo "Cleaning up..."
	rm -f $(PLUGIN_SO) $(TEST_APP) *.o

.PHONY: all test clean


