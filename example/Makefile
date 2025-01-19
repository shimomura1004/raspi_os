SUBDIRS = ./test_binary ./echo ./mini-os

all: $(SUBDIRS)
clean: $(SUBDIRS)

$(SUBDIRS): FORCE
	$(MAKE) -C $@ $(MAKECMDGOALS)

FORCE:
