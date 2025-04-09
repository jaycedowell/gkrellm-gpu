PLUGIN_NAME = gpu-plugin

CC ?= gcc
CFLAGS ?= -O2 -fPIC
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-2.0 gthread-2.0)
GKRELLM_INCLUDE = -I/usr/include
NVML_INCLUDE = -I/usr/local/include

LIBS = $(shell pkg-config --libs gtk+-2.0)
NVML_LIBS = -lnvidia-ml
PLUGIN_DIR ?= $(HOME)/.gkrellm2/plugins

OBJS = gpu-plugin.o

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(OBJS)
	$(CC) $(OBJS) -o $(PLUGIN_NAME).so -shared $(LIBS) $(NVML_LIBS)

.c.o:
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(GKRELLM_INCLUDE) $(NVML_INCLUDE) -c $< -o $@

clean:
	rm -f *.o *.so

install:
	mkdir -p $(PLUGIN_DIR)
	cp $(PLUGIN_NAME).so $(PLUGIN_DIR)
