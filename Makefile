subdirs := ramses profile

all: profile

.PHONY: all clean $(subdirs)

$(subdirs):
	@$(MAKE) -C $@

profile: ramses

clean:
	@for d in $(subdirs); do $(MAKE) -C $$d clean; done
