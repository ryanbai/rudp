include ./lwip.mk

project.targets := liblwip
liblwip.name := liblwip.a
liblwip.type := lib
liblwip.path := lib
all_sources := $(shell find . -name "*.c")
liblwip.sources := $(all_sources)
liblwip.debug=1

include ./inc.mk

gendep:
	@echo "generate ${project.targets} depend file ok."
