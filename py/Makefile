all: pyramses

pyramses:
	$(MAKE) -C $@

.PHONY: clean pyramses

clean:
	$(MAKE) -C pyramses/ clean
	rm -rf __pycache__ hammertime/__pycache__
