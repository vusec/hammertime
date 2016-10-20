PROF2TBL := ../tools/py/hammerprof.py

tbl_files := $(patsubst %.res,%.fliptbl,$(wildcard */*.res) $(wildcard *.res))


all: $(tbl_files)

%.fliptbl: %.res
	$(PROF2TBL) $< $@


.PHONY: clean

clean:
	rm -f *.fliptbl
	rm -f */*.fliptbl
