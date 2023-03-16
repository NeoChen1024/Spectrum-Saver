CC	= cc
CXX	= c++
OPT	= -Og -pipe -fPIC -fPIE
IMAGEMAGICK_FLAGS = $(shell Magick++-config --cxxflags --libs)
FLAGS	= $(OPT) -I. -g3 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200809L $(IMAGEMAGICK_FLAGS)
#DBG	= -fsanitize=undefined,integer,nullability -fno-omit-frame-pointer
CFLAGS	= $(FLAGS) $(DBG) -std=c99
CXXFLAGS = $(FLAGS) $(DBG) -std=c++20
PRGS	= spsave log2png

.PHONY: all clean countline

all: $(PRGS)

countline:
	wc -l *.h *.c

clean:
	rm -f $(OBJS) $(PRGS) *.log
