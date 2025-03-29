SUBDIRS = ./test_binary ./echo ./mini-os ./raspios

all: $(SUBDIRS)
clean: $(SUBDIRS)

$(SUBDIRS): FORCE
	$(MAKE) -C $@ $(MAKECMDGOALS)

FORCE:
