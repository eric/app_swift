#
# Swift Text-To-Speech application module for the Asterisk PBX
#
# Copyright (C) 2004,2005 mezzoConsult C.B.
#
# Sven Slezak <sunny@mezzo.net>
#

NAME=app_swift
CONF=swift.conf

ASTINC=/usr/include/asterisk
MODULES_DIR=/usr/lib/asterisk/modules

CC=gcc
CFLAGS=$(shell ./cflags.sh)

OSARCH=$(shell uname -s)

ifeq ($(OSARCH),Darwin)
  CFLAGS+= -D__Darwin__
	SOLINK=-dynamic -bundle -undefined suppress -force_flat_namespace -framework swift
	TESTLINK=-undefined suppress -force_flat_namespace -framework swift
  ASTINC=/Library/Asterisk/include
  MODULES_DIR=/Library/Asterisk/modules
  CONF_DIR=/Library/Asterisk/conf
  CC=gcc -arch ppc -arch i386
else
	SWIFT_DIR=/opt/swift
	CFLAGS+= -I${SWIFT_DIR}/include
	LDFLAGS=-L${SWIFT_DIR}/lib -lswift -lm $(patsubst ${SWIFT_DIR}/lib/lib%.so,-l%,$(wildcard ${SWIFT_DIR}/lib/libcep*.so))
	SOLINK=-shared -Xlinker -x
  CONF_DIR=/etc/asterisk
endif

RES=$(shell if [ -f ${ASTINC}/channel.h ]; then echo "$(NAME).so"; fi)

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
