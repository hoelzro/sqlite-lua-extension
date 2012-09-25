CFLAGS+=-fPIC

ifeq ($(shell uname), Darwin)
DYNEXT=dylib
else
DYNEXT=so
endif

default: lua.$(DYNEXT)

lua.dylib: lua.o
	gcc -o $@ -bundle -undefined dynamic_lookup $^ -llua -lm -ldl

lua.so: lua.o
	gcc -o $@ -shared $^ -llua -lm -ldl

clean:
	rm -f *.o *.$(DYNEXT)
