CC	= cc
CXX	= c++
OPT	= -O2 -pipe -fPIC -fPIE -flto=auto
IMAGEMAGICK_LIBS = $(shell Magick++-config --libs)
IMAGEMAGICK_FLAGS = $(shell Magick++-config --cxxflags)
FMT_LIB = -lfmt
FLAGS	= $(OPT) -I./include -g3 -pedantic -Wall -Wextra $(IMAGEMAGICK_FLAGS)
LIBS	= $(IMAGEMAGICK_LIBS) $(FMT_LIB)
#DBG	= -fsanitize=undefined,integer,nullability -fno-omit-frame-pointer
CXXFLAGS = $(FLAGS) $(DBG) -std=c++17
OBJS	= spsave.o log2png.o common.o
PRGS	= spsave log2png

.PHONY: all clean strip

all: $(PRGS)

log2png: log2png.o common.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

spsave: spsave.o common.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(OBJS) $(PRGS)
