############################################
# Auto generated by configure tool.
# Copyright by Lovelace.Lee.
# Building time 2016-01-19-12:51:21.
############################################
SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c, %.o,$(SRCS))
OBDS := $(patsubst %.c, %.d,$(SRCS))

.PHONY: all clean distclean
all:$(OBJS)
%.o:%.c
	@echo "Building file: $(notdir $<) ..."
	gcc -c -Wall -O3 -Wno-psabi   -o"$@" "$<"
	@echo "Finished building: $(notdir $@)"
clean:
	rm -rf $(OBJS) $(OBDS)
distclean:
	$(MAKE) clean
	rm -f .makefile