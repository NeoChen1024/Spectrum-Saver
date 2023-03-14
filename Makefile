CC	= cc
CXX	= c++
OPT	= -O2 -pipe -fPIC -fPIE
FLAGS	= $(OPT) -I. -g3 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200809L
#DBG	= -fsanitize=undefined,integer,nullability -fno-omit-frame-pointer
CFLAGS	= $(FLAGS) $(DBG) -std=c99
CXXFLAGS = $(FLAGS) $(DBG) -std=c++20 -stdlib=libc++
LDFLAGS	= -Wl,-O1 -Wl,--as-needed
PRGS	= spsave

.PHONY: all clean countline

all: $(PRGS)

countline:
	wc -l *.h *.c

clean:
	rm -f $(OBJS) $(PRGS)
