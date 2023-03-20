CC	= cc
CXX	= c++
OPT	= -O2 -pipe -fPIC -fPIE
IMAGEMAGICK_FLAGS = $(shell Magick++-config --cxxflags --libs)
FMT_LIB = -lfmt
FLAGS	= $(OPT) -I./include -g3 -pedantic -Wall -Wextra $(IMAGEMAGICK_FLAGS) $(FMT_LIB)
#DBG	= -fsanitize=undefined,integer,nullability -fno-omit-frame-pointer
CXXFLAGS = $(FLAGS) $(DBG) -std=c++20
PRGS	= spsave log2png

.PHONY: all clean strip

all: $(PRGS)

clean:
	rm -f $(OBJS) $(PRGS) *.o
