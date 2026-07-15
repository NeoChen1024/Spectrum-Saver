CXX ?= c++

OPT ?= -O2 -pipe -flto=auto
WARNINGS := -Wall -Wextra -Wpedantic
CPPFLAGS := -I. -I./include
CXXFLAGS := $(OPT) -g3 $(WARNINGS) -std=c++20 -MMD -MP
OPENMP_FLAGS := -fopenmp
IMAGEMAGICK_FLAGS := $(filter-out -fopenmp,$(shell Magick++-config --cxxflags))
IMAGEMAGICK_LIBS := $(shell Magick++-config --libs)

COMMON_OBJS := common.o
SPSAVE_OBJS := spsave.o serial_protocol.o $(COMMON_OBJS)
LOG2PNG_OBJS := log2png.o $(COMMON_OBJS)
TEST_OBJS := tests/test_main.o serial_protocol.o $(COMMON_OBJS)
OBJS := spsave.o serial_protocol.o log2png.o common.o tests/test_main.o
DEPS := $(OBJS:.o=.d)
PRGS := spsave log2png

.PHONY: all clean test

all: $(PRGS)

log2png.o: CPPFLAGS += $(IMAGEMAGICK_FLAGS)
log2png.o: CXXFLAGS += $(OPENMP_FLAGS)

log2png: $(LOG2PNG_OBJS)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $^ $(IMAGEMAGICK_LIBS)

spsave: $(SPSAVE_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

tests/test_main: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

test: tests/test_main
	./tests/test_main

clean:
	rm -f $(OBJS) $(DEPS) $(PRGS) tests/test_main

-include $(DEPS)
