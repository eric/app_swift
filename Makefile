NAME=app_swift
CONF=swift.conf

CC     ?= gcc
OSARCH ?= $(shell uname -s)

SWIFT_DIR     ?= /opt/swift
ASTERISK_ROOT ?= /usr
MODULES_DIR   ?= $(ASTERISK_ROOT)/lib/asterisk/modules

CFLAGS=-I${SWIFT_DIR}/include -I$(ASTERISK_ROOT)/include -g -Wall -D_REENTRANT -D_GNU_SOURCE -fPIC
LDFLAGS=-L${SWIFT_DIR}/lib -lswift -lm -lswift $(patsubst ${SWIFT_DIR}/lib/lib%.so,-l%,$(wildcard ${SWIFT_DIR}/lib/libcep*.so))
SOLINK=-shared -Xlinker -x

RES=$(shell if [ -f $(ASTERISK_ROOT)/include/asterisk/channel.h ]; then echo "$(NAME).so"; fi)


$(NAME).so : $(NAME).o
	$(CC) $(SOLINK) -o $@ $(LDFLAGS) $<

all: $(RES)

clean:
	rm -f $(NAME).o $(NAME).so

install: all
	if ! [ -f /etc/asterisk/$(CONF) ]; then \
		install -m 644 $(CONF).sample /etc/asterisk/$(CONF) ; \
	fi
	if [ -f $(NAME).so ]; then \
		install -m 755 $(NAME).so $(MODULES_DIR) ; \
	fi

reload: install
	asterisk -rx "module unload ${RES}"
	asterisk -rx "module load ${RES}"
