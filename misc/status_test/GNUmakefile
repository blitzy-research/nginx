
# The tests of the HTTP status code registry.  They are made on request, of a
# tree that has already been configured and made:
#
#     ./auto/configure
#     make
#     make -f misc/status_test/GNUmakefile
#
# and the targets are "test", which is what "all" is, "check" and "clean".
# Nothing of auto/configure, of the makefile it generates or of the build of
# nginx reaches this file: it is made by hand, and only when the registry is
# being worked on.
#
# What it does is compile the driver beside it with the compiler, the flags and
# the include paths of the build, then link the driver against the objects the
# build left, leaving out the one object of the tree that defines main() and
# adding the object of the generated ngx_modules.c by name.  All of that it
# reads out of the makefile the build generated, as text: that makefile has a
# default target of its own, so including it would hand this one over, and it
# is never written to either.
#
# Both the object and the program are written under objs/, which is ignored,
# so that a run of the tests leaves nothing behind to be committed.


NGX_MAKEFILE =	objs/Makefile

TEST_SRC =	misc/status_test/ngx_http_status_test.c
TEST_OBJ =	objs/misc/status_test/ngx_http_status_test.o
TEST_BIN =	objs/ngx_http_status_test

# The object of the sole main() of the tree, which the driver replaces, and
# the object of the generated ngx_modules.c, which is written beside objs/
# rather than under objs/src/ and so is named here: ngx_modules[] and
# ngx_module_names[] are defined there and ngx_preinit_modules() reads both.

MAIN_OBJ =	objs/src/core/nginx.o
MODULES_OBJ =	objs/ngx_modules.o

# The registry itself, which auto/modules names in ngx_module_srcs of the HTTP
# core, so that a build has it whether or not it validates.

STATUS_OBJ =	objs/src/http/ngx_http_status.o


all:		test


.PHONY:		all check clean test


# The objects of a directory and of every directory under it.  GNU make has no
# wildcard that descends, so this descends by itself.

ngx_objs =	$(foreach d,$(wildcard $(1)/*),$(call ngx_objs,$(d))	\
			$(filter %.o,$(d)))


# The value of one assignment of the generated makefile, with the lines a
# backslash continues joined into one and the indentation of those lines left
# out.  $(1) is the name assigned to.

ngx_value =	awk '{ if (!f) { if (!/^$(1)[ \t]*=/) next;		\
			f = 1; sub(/^[^=]*=/, "") };			\
		c = /\\$$/; sub(/[ \t]*\\$$/, ""); sub(/^[ \t]+/, "");	\
		sub(/[ \t]+$$/, ""); printf "%s ", $$0;			\
		if (!c) exit }' $(NGX_MAKEFILE)


# The recipe the generated makefile links nginx with, read the same way.  $(1)
# is "objs" for the objects it names, or "tail" for everything it puts after
# them: NGX_LD_OPT and CORE_LIBS, then CORE_LINK, which carries the perl embed
# options of a build with perl, then MAIN_LINK.  The order of that tail and
# every repetition in it are kept as found, a linker reading a library list in
# order, so no token of it is sorted away or spelled out here.

ngx_link =	awk -v w=$(1) '{ if (!f) { if (!/^\t.*\(LINK\)/) next;	\
			f = 1; sub(/^.*\(LINK\)[ \t]*/, "") };		\
		c = /\\$$/; sub(/[ \t]*\\$$/, "");			\
		n = split($$0, t, /[ \t]+/);				\
		for (i = 1; i <= n; i++) {				\
			if (t[i] == "") continue;			\
			if (s) { s = 0; continue };			\
			if (t[i] == "-o") { s = 1; continue };		\
			if (t[i] ~ /\.o$$/) { if (w == "objs")		\
				printf "%s ", t[i]; continue };		\
			if (w == "tail") printf "%s ", t[i] };		\
		if (!c) exit }' $(NGX_MAKEFILE)


ifeq ($(wildcard $(NGX_MAKEFILE)),)


# Nothing has been configured, so there is nothing to read and nothing to
# link.  No target but clean is reachable, and none of the reading above is
# reached: every variable that reads is expanded only where it is used.

check test $(TEST_OBJ) $(TEST_BIN):
	@echo "$(NGX_MAKEFILE) is not there.  nginx has to be"		\
		"configured and made first:" >&2
	@echo "" >&2
	@echo "    ./auto/configure" >&2
	@echo "    make" >&2
	@echo "    make -f misc/status_test/GNUmakefile" >&2
	@exit 1


else


# The compiler, the flags and the include paths of the build.  Each is an
# ordinary variable, so that a run may say CC=, CFLAGS= or ALL_INCS= to
# compile the driver its own way, and none of them is read until it is used.

CC =		$(shell $(call ngx_value,CC))
CFLAGS =	$(shell $(call ngx_value,CFLAGS))
ALL_INCS =	$(shell $(call ngx_value,ALL_INCS))

# The command the build links with, which the generated makefile writes as
# $(CC).  The text of a make variable reaches a shell unexpanded, where it
# would be read as a command substitution, so it is resolved here instead.

NGX_LINK =	$(shell $(call ngx_value,LINK))
LINK =		$(subst $$(CC),$(CC),$(NGX_LINK))
LINK_TAIL =	$(shell $(call ngx_link,tail))

# Every object of the build, found rather than listed so that whatever it was
# configured to compile is what is linked, narrowed to the objects its own
# recipe hands the linker: a build with perl also leaves the object of the XS
# glue under objs/src, and that one belongs to a shared object of its own.
# The object of the sole main() is what is left out, and nothing else is.

TREE_OBJS =	$(sort $(call ngx_objs,objs/src))
RECIPE_OBJS =	$(shell $(call ngx_link,objs))
LINK_OBJS =	$(filter-out $(MAIN_OBJ),				\
			$(filter $(RECIPE_OBJS),$(TREE_OBJS)))		\
		$(MODULES_OBJ)


test:		check $(TEST_BIN)
	$(TEST_BIN)


# The driver reaches the registry through <ngx_http.h>, as every other file of
# the tree does, and the generated configuration header is what says whether
# the build validates, so a change to any of the three is a change to what is
# being tested here.

$(TEST_OBJ):	$(TEST_SRC) src/http/ngx_http_status.h			\
			src/http/ngx_http.h objs/ngx_auto_config.h	\
			| check
	mkdir -p objs/misc/status_test
	$(CC) -c $(CFLAGS) $(ALL_INCS)					\
		-o $(TEST_OBJ) $(TEST_SRC)


$(TEST_BIN):	$(TEST_OBJ) $(LINK_OBJS)
	$(LINK) -o $(TEST_BIN) $(TEST_OBJ) $(LINK_OBJS)			\
		$(LINK_TAIL)


# What the tests need of a build and may not find.  A build with no registry
# object is one whose auto/modules does not name src/http/ngx_http_status.c,
# which is a want of that file rather than of anything here.

check:
	@test -n "$(filter objs,$(ALL_INCS))" || {			\
		echo "$(NGX_MAKEFILE) has no -I objs among its include"	\
			"paths, so the generated headers of the build"	\
			"cannot be found" >&2;				\
		exit 1; }
	@for o in objs/nginx $(MODULES_OBJ) $(MAIN_OBJ); do		\
		test -f $$o && continue;				\
		echo "$$o is not there, so the build is unfinished:"	\
			"run make first" >&2;				\
		exit 1;							\
	done
	@test -f $(STATUS_OBJ) || {					\
		echo "$(STATUS_OBJ) is not there:"			\
			"src/http/ngx_http_status.c has to be named in"	\
			"ngx_module_srcs in auto/modules" >&2;		\
		exit 1; }


endif


# Only what the targets above write, and harmless where they have written
# nothing at all.

clean:
	rm -f objs/ngx_http_status_test
	rm -rf objs/misc/status_test
