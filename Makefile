CC	= cc
CXX	= c++
OPT	= -O2 -pipe -fPIC -fPIE
IMAGEMAGICK_FLAGS = $(shell Magick++-config --cxxflags --libs)
FLAGS	= $(OPT) -I./include -g3 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200809L $(IMAGEMAGICK_FLAGS)
#DBG	= -fsanitize=undefined,integer,nullability -fno-omit-frame-pointer
CFLAGS	= $(FLAGS) $(DBG) -std=c99
CXXFLAGS = $(FLAGS) $(DBG) -std=c++20
PRGS	= spsave log2png

.PHONY: all clean countline cleanfmt

all: $(PRGS)

log2png: log2png.o libfmt.a
	$(CXX) $(CXXFLAGS) -o $@ $^

spsave: spsave.o libfmt.a
	$(CXX) $(CXXFLAGS) -o $@ $^

# It uses CMake, so we need to build it separately
libfmt.a:
	cd contrib/fmt && cmake -Bbuild -H. -DFMT_TEST=OFF -DFMT_DOC=OFF -DFMT_INSTALL=OFF
	$(MAKE) -C contrib/fmt/build
	ln -s contrib/fmt/build/libfmt.a .

cleanfmt:
	rm -f libfmt.a
	rm -rf contrib/fmt/build

countline:
	wc -l *.h *.c

clean: cleanfmt
	rm -f $(OBJS) $(PRGS) *.o *.log
