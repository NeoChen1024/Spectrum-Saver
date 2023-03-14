CC	= cc
CXX	= c++
OPT	= -O2 -pipe -fPIC -fPIE
FLAGS	= $(OPT) -I. -g3 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200809L
#DBG	= -fsanitize=undefined,integer,nullability -fno-omit-frame-pointer
CFLAGS	= $(FLAGS) $(DBG) -std=c99
CXXFLAGS = $(FLAGS) $(DBG) -std=c++20
LDFLAGS	= -Wl,-O1 -Wl,--as-needed
OBJS	= main.o
PRGS	= spsave

.PHONY: all clean countline

all: $(PRGS)

spsave: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) -o $@

countline:
	wc -l *.h *.c

clean:
	rm -f $(OBJS) $(PRGS)
