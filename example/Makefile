SUBDIRS = ./test_binary ./echo ./mini-os ./raspios ./vmm

all: $(SUBDIRS)
clean: $(SUBDIRS)

$(SUBDIRS): FORCE
	$(MAKE) -C $@ $(MAKECMDGOALS)

FORCE:
