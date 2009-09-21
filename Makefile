#
# Swift Text-To-Speech application module for the Asterisk PBX
#
# Copyright (C) 2004,2005 mezzoConsult C.B.
#
# Sven Slezak <sunny@mezzo.net>
#

NAME=app_swift
CONF=swift.conf

CC      = gcc
CFLAGS ?= -g -Wall -D_REENTRANT -D_GNU_SOURCE -fPIC
OSARCH  =$(shell uname -s)

SWIFT_DIR ?= /opt/swift

ifeq ($(OSARCH),Darwin)
  AST_DIR     ?= /Library/Asterisk
  MODULES_DIR ?= $(AST_DIR)/modules
  CONF_DIR    ?= $(AST_DIR)/conf
else
  AST_DIR     ?= /usr
  MODULES_DIR ?= $(AST_DIR)/lib/asterisk/modules
  CONF_DIR    ?= /etc/asterisk
endif

CFLAGS += -I$(AST_DIR)/include -DAST_MODULE=\"$(NAME)\"

ifneq ($(shell grep -c ast_config_load $(AST_DIR)/include/asterisk/channel.h),0)
	CFLAGS += -DCHANNEL_HAS_CID
endif

ifneq ($(shell grep -c ast_config_load $(AST_DIR)/include/asterisk/config.h),0)
	CFLAGS += -DNEW_CONFIG
endif


ifeq ($(OSARCH),Darwin)
  CFLAGS += -D__Darwin__
	SOLINK=-dynamic -bundle -undefined suppress -force_flat_namespace -framework swift
	TESTLINK=-undefined suppress -force_flat_namespace -framework swift
  CC=gcc -arch ppc -arch i386
else
  CFLAGS    += -I${SWIFT_DIR}/include
  LDFLAGS   =  -L${SWIFT_DIR}/lib -lswift -lm $(patsubst ${SWIFT_DIR}/lib/lib%.so,-l%,$(wildcard ${SWIFT_DIR}/lib/libcep*.so))
  SOLINK    =  -shared -Xlinker -x
endif

RES=$(shell if [ -f $(AST_DIR)/include/asterisk/channel.h ]; then echo "$(NAME).so"; fi)

$(NAME).so : $(NAME).o
	$(CC) $(SOLINK) -o $@ $(LDFLAGS) $<

test: test.o
	$(CC) $(TESTLINK) -o $@ $(LDFLAGS) $<

all: $(RES)

clean:
	rm -f $(NAME).o $(NAME).so

install: all
	if ! [ -f $(CONF_DIR)/$(CONF) ]; then \
		install -m 644 $(CONF).sample $(CONF_DIR)/$(CONF) ; \
	fi
	if [ -f $(NAME).so ]; then \
		install -m 755 $(NAME).so $(MODULES_DIR) ; \
	fi

reload: install
	asterisk -rx "unload ${RES}"
	asterisk -rx "load ${RES}"
