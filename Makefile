CXX ?= c++
.DEFAULT_GOAL := all

OPT ?= -O2 -pipe -flto=auto
WARNINGS := -Wall -Wextra -Wpedantic
CPPFLAGS := -I. -I./include
CXXFLAGS := $(OPT) -g3 $(WARNINGS) -std=c++20 -MMD -MP
THREAD_FLAGS := -pthread
OPENMP_FLAGS := -fopenmp
IMAGEMAGICK_FLAGS := $(filter-out -fopenmp,$(shell Magick++-config --cxxflags))
IMAGEMAGICK_LIBS := $(shell Magick++-config --libs)

include contrib/crc32c.mk

COMMON_OBJS := common.o log_io.o
SPSAVE_OBJS := spsave.o serial_protocol.o $(COMMON_OBJS)
LOG2PNG_OBJS := log2png.o $(COMMON_OBJS)
SPLOGCONVERT_OBJS := splogconvert.o $(COMMON_OBJS)
TEST_OBJS := tests/test_main.o serial_protocol.o $(COMMON_OBJS)
OBJS := spsave.o serial_protocol.o log2png.o splogconvert.o common.o log_io.o tests/test_main.o
DEPS := $(OBJS:.o=.d) $(CRC32C_DEPS)
PRGS := spsave log2png splogconvert

.PHONY: all clean test

all: $(PRGS)

log2png.o: CPPFLAGS += $(IMAGEMAGICK_FLAGS)
log2png.o: CXXFLAGS += $(OPENMP_FLAGS)

log2png: $(LOG2PNG_OBJS) $(CRC32C_LIB)
	$(CXX) $(CXXFLAGS) $(THREAD_FLAGS) $(OPENMP_FLAGS) -o $@ $^ $(IMAGEMAGICK_LIBS)

spsave: $(SPSAVE_OBJS) $(CRC32C_LIB)
	$(CXX) $(CXXFLAGS) $(THREAD_FLAGS) -o $@ $^

splogconvert: $(SPLOGCONVERT_OBJS) $(CRC32C_LIB)
	$(CXX) $(CXXFLAGS) $(THREAD_FLAGS) -o $@ $^

tests/test_main: $(TEST_OBJS) $(CRC32C_LIB)
	$(CXX) $(CXXFLAGS) $(THREAD_FLAGS) -o $@ $^

test: tests/test_main
	./tests/test_main

clean:
	rm -f $(OBJS) $(DEPS) $(PRGS) tests/test_main
	rm -rf .build

-include $(DEPS)
