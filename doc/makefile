TARGETS=alerts_statuses.svg paramset_hierarchy.svg paramset_hierarchy_caption.svg taskinstances_statuses.svg 

.PHONY: default all clean re

.SUFFIXES: .dot .png .svg

.dot.png:
	dot -Tpng -o $@ $<

.dot.svg:
	dot -Tsvg -o $@ $<

default: all

all: $(TARGETS)

clean:
	-rm $(TARGETS)

re: clean all

