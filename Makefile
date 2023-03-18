CC	= cc
CXX	= c++
OPT	= -O2 -pipe -fPIC -fPIE
IMAGEMAGICK_FLAGS = $(shell Magick++-config --cxxflags --libs)
FLAGS	= $(OPT) -I./include -g3 -pedantic -Wall -Wextra $(IMAGEMAGICK_FLAGS)
#DBG	= -fsanitize=undefined,integer,nullability -fno-omit-frame-pointer
CFLAGS	= $(FLAGS) $(DBG) -std=c99
CXXFLAGS = $(FLAGS) $(DBG) -std=c++20
PRGS	= spsave log2png

.PHONY: all clean strip

all: $(PRGS)

clean:
	rm -f $(OBJS) $(PRGS) *.o
