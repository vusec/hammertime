subdirs := profile probe_test

all: $(subdirs)

.PHONY: clean $(subdirs)

$(subdirs):
	@$(MAKE) -C $@

clean:
	@for d in $(subdirs); do $(MAKE) -C $$d clean; done
	rm -rf py/__pycache__
