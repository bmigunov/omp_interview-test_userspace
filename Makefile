SUBDIRS = $(shell find . -maxdepth 1 -mindepth 1 -type d -printf "%f\n")

all:
	@list='$(SUBDIRS)';               \
	for subdir in $$list; do          \
		echo "MAKING $@ IN $$subdir"; \
		(cd $$subdir && make $@)      \
	done
