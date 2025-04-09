PLUGIN_NAME = gpu-plugin

CC ?= gcc
CFLAGS ?= -O2 -fPIC
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-2.0 gthread-2.0)
GKRELLM_INCLUDE = -I/usr/include
NVML_CFLAGS = $(shell pkg-config --cflags nvidia-ml-12.6 2>/dev/null)

LIBS = $(shell pkg-config --libs gtk+-2.0)
NVML_LIBS = $(shell pkg-config --libs nvidia-ml-12.6 2>/dev/null || echo "-lnvidia-ml")
PLUGIN_DIR ?= $(HOME)/.gkrellm2/plugins

OBJS = gpu-plugin.o

.PHONEY: all clean install test

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(OBJS)
	$(CC) $(OBJS) -o $(PLUGIN_NAME).so -shared $(LIBS) $(NVML_LIBS)

.c.o:
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(GKRELLM_INCLUDE) $(NVML_CFLAGS) -c $< -o $@

test: $(PLUGIN_NAME).so
	$(MAKE) -C tests
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):. tests/test-linking

clean:
	rm -f *.o *.so
	$(MAKE) -C tests clean

install:
	mkdir -p $(PLUGIN_DIR)
	cp $(PLUGIN_NAME).so $(PLUGIN_DIR)

