NSLIBS := libwapcaplet libparserutils libhubbub libdom libcss libnsutils \
	libnsbmp libnsgif

NSLIBS_PREFIX ?= $(CURDIR)/inst
NSLIBS_OBJROOT ?= $(CURDIR)/obj

.PHONY: all $(NSLIBS)

all: $(NSLIBS)

libhubbub: libparserutils
libdom: libwapcaplet libparserutils libhubbub
libcss: libwapcaplet libparserutils

$(NSLIBS):
	@$(MAKE) --no-print-directory -C $@ \
		PREFIX=$(NSLIBS_PREFIX) \
		NSSHARED=$(CURDIR)/buildsystem \
		COMPONENT_TYPE=lib-static \
		BUILDDIR=$(NSLIBS_OBJROOT)/$@ \
		install
