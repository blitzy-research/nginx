
# The tests of the HTTP status code registry.  They are made on request, of a
# tree that has already been configured and made:
#
#     ./auto/configure
#     make
#     make -f misc/status_test/GNUmakefile
#
# and the targets are "test", which is what "all" is, "check", "metadata" and
# "clean".  Nothing of auto/configure, of the makefile it generates or of the
# build of nginx reaches this file: it is made by hand, and only when the
# registry is being worked on.
#
# What it does is compile the driver beside it with the compiler, the flags and
# the include paths of the build, then link the driver against the objects the
# recipe of the build hands the linker, leaving out the one object of the tree
# that defines main().  All of that it reads out of the makefile the build
# generated, as text: that makefile has a default target of its own, so
# including it would hand this one over, and it is never written to either.
#
# What the driver cannot reach, being linked against the registry rather than
# driving a running nginx, is reached by the external nginx test harness, which
# is not part of this repository and is run from its own checkout.
#
# "metadata" reads the table the registry is built from, also as text, and
# says whether the flags and the provenance of every row of it answer for the
# class of the code, and then whether the reference the registry publishes
# describes each of those rows the same way.  The driver cannot ask either of
# the registry, which publishes no accessor for a row's flags or for where it
# records a code as coming from.
#
# Both the object and the program are written under objs/, which is ignored,
# so that a run of the tests leaves nothing behind to be committed.


NGX_MAKEFILE =	objs/Makefile

TEST_MAKEFILE =	misc/status_test/GNUmakefile
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
# core, so that a build has it whether or not it validates, and the source it
# is compiled from, which the metadata target reads.

STATUS_OBJ =	objs/src/http/ngx_http_status.o
STATUS_SRC =	src/http/ngx_http_status.c

# The reference the metadata target holds that source to, which publishes a row
# of its own for every code the registry describes.

STATUS_MD =	docs/api/status_codes.md


all:		test


.PHONY:		all check clean metadata test


# An object of the build named as a prerequisite and not yet compiled
# would otherwise be compiled by make itself, out of its own built-in
# rule and with flags of its own rather than the flags the build was
# configured with, and the staleness check below would then be answered
# by an object this makefile had made instead of reporting that the
# build had not made it.  A rule of the same shape and no recipe cancels
# the built-in one.  The build is the only thing that compiles the
# build.

%.o:		%.c


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


# The rows of the table of definitions the registry is built from, three
# lines of one row at a time: the code and the phrase it sends, then the
# flags, then where it comes from.  A row of a class carries the flag of that
# class and no other, the six codes nginx keeps for itself are the six rows
# flagged internal, every row records a provenance, and a row whose phrase
# departs from RFC 9110 records the name it departs from.  What is wrong with
# a row is printed with the code that names it, and a read that prints
# anything ends non-zero.  $(1) is the file to read.

ngx_metadata =	awk 'function no(c, m) {				\
		printf "%s: %d: %s\n", FILENAME, c, m; e++ };		\
		function row() { n++; split("", h);			\
			k = split(fw, f, /\|/);				\
			for (i = 1; i <= k; i++) { h[f[i]] = 1;		\
				if (f[i] != "0" && !(f[i] in ok))	\
					no(c, "unknown flag " f[i]) };	\
			if (n == 1) { if (c || ph != ""			\
				|| fw != "0" || rs != "")		\
					no(c, "not the empty sentinel");\
				return };				\
			if (c <= pv) no(c, "not above " pv);		\
			pv = c;						\
			if (c < 100 || c > 599)				\
				no(c, "outside 100..599");		\
			g = (f1 in h) (f4 in h) (f5 in h);		\
			w = (c < 200) ? "100" : (c >= 500) ? "001"	\
				: (c >= 400) ? "010" : "000";		\
			if (g != w)					\
				no(c, "class flags " g ", want " w);	\
			if (fi in h) { iv++;				\
				if (!(c in own))			\
					no(c, "flagged internal,"	\
						" not one of the six");	\
				if (k != 2 || !(f4 in h))		\
					no(c, "internal, want " f4	\
						"|" fi);		\
				if (rs != ni)				\
					no(c, "internal, want a " ni	\
						" provenance");		\
				return };				\
			if (c in own)					\
				no(c, "one of the six, not flagged"	\
					" internal");			\
			if (rs == "") { no(c, "records no provenance");	\
				return };				\
			if (rs !~ /^RFC [0-9]+ section /)		\
				no(c, "no RFC section in provenance");	\
			if (match(rs, / \([^()]+\)$$/)) { dv++;		\
				m = substr(rs, RSTART + 2, RLENGTH - 3);\
				if (!(c in want))			\
					no(c, "records a name, but"	\
						" does not depart");	\
				else if (m != want[c])			\
					no(c, "records " m ", RFC 9110"	\
						" reads " want[c]);	\
				else if (ph == c " " m)			\
					no(c, "sends the very name"	\
						" it departs from");	\
				else if (ph == "")			\
					no(c, "sends no phrase to"	\
						" depart with") }	\
			else if (c in want)				\
				no(c, "departs from RFC 9110, so it"	\
					" records " want[c]) };		\
		BEGIN { ni = "nginx internal";				\
			k = split("CACHEABLE INFORMATIONAL"		\
				" CLIENT_ERROR SERVER_ERROR EXPIRES_OK"	\
				" INTERNAL", z, " ");			\
			for (i = 1; i <= k; i++)			\
				ok["NGX_HTTP_STATUS_" z[i]] = 1;	\
			f1 = "NGX_HTTP_STATUS_" z[2];			\
			f4 = "NGX_HTTP_STATUS_" z[3];			\
			f5 = "NGX_HTTP_STATUS_" z[4];			\
			fi = "NGX_HTTP_STATUS_" z[6];			\
			k = split("444 494 495 496 497 499", z, " ");	\
			for (i = 1; i <= k; i++) own[z[i]] = 1;		\
			k = split("302 405 408 413 414 416 503 504",	\
				z, " ");				\
			split("Found|Method Not Allowed|Request"	\
				" Timeout|Content Too Large|URI Too"	\
				" Long|Range Not Satisfiable|Service"	\
				" Unavailable|Gateway Timeout", y, "|");\
			for (i = 1; i <= k; i++) want[z[i]] = y[i] };	\
		/^\#define[ \t]+NGX_HTTP_STATUS_NBUILTIN/ {		\
			nb = $$3 + 0; next };				\
		/^static ngx_http_status_def_t				\
			ngx_http_status_defs\[/ { t = 1; next };	\
		t && /^};/ { t = 0; next };				\
		!t { next };						\
		/^[ \t]*\{[ \t]*[0-9]/ { c = $$2 + 0; ph = "";		\
			if (!/ngx_null_string/) { ph = $$0;		\
				sub(/^[^"]*"/, "", ph);			\
				sub(/".*$$/, "", ph) };			\
			p = 1; next };					\
		p == 1 { fw = $$1; sub(/,$$/, "", fw);			\
			p = 2; next };					\
		p == 2 { rs = ""; if ($$1 != "NULL") { rs = $$0;	\
				sub(/^[^"]*"/, "", rs);			\
				sub(/".*$$/, "", rs) };			\
			p = 0; row(); next };				\
		END { if (t) { e++;					\
				print "the table does not end" };	\
			if (!nb) { e++;					\
				print "NGX_HTTP_STATUS_NBUILTIN is not"	\
					" defined" };			\
			if (nb && n - 1 != nb) { e++;			\
				printf "%d rows after the sentinel,"	\
					" NGX_HTTP_STATUS_NBUILTIN says"\
					" %d\n", n - 1, nb };		\
			if (iv != 6) { e++;				\
				printf "%d rows are flagged internal,"	\
					" not 6\n", iv };		\
			if (dv != 8) { e++;				\
				printf "%d rows record a name of RFC"	\
					" 9110, not 8\n", dv };		\
			if (e) exit 1 }' $(1)


# The reference the registry publishes, read the same way and required to say
# what the source says: every described code once in each of them, and for each
# of them the same phrase, the same flags whatever order they are written in,
# and the same provenance.  A section number mistyped, or moved from one code to
# another, is then a disagreement between two files written by hand
# independently, which is the only way it can be seen at all: the provenance of
# a row is the one member no function of the registry answers from, so no test
# the driver runs can ask for it.  $(1) is the reference to read, $(2) the
# source.

ngx_agree =	awk 'function no(f, c, m) {				\
		printf "%s: %d: %s\n", f, c, m; e++ };			\
		function srt(s, n, a, i, j, t, o) {			\
			n = split(s, a, ",");				\
			for (i = 2; i <= n; i++)			\
				for (j = i; j > 1 && a[j] < a[j - 1];	\
					j--) { t = a[j];		\
					a[j] = a[j - 1];		\
					a[j - 1] = t };			\
			o = "";						\
			for (i = 1; i <= n; i++)			\
				o = o (i > 1 ? "," : "") a[i];		\
			return o };					\
		function key(ph, fl, rs) {				\
			return ph "|" srt(fl) "|" rs };			\
		FNR == NR && /^\| Code \| .reason. \| Flags \|/ {	\
			m = 1; md = FILENAME; next };			\
		FNR == NR && m && $$0 !~ /^\|/ { m = 0; next };		\
		FNR == NR { if (!m || split($$0, f, "|") != 6		\
				|| f[2] + 0 < 100) next;		\
			c = f[2] + 0; ph = f[3];			\
			fl = f[4]; rs = f[5];				\
			gsub(/[`\t ]/, "", fl);				\
			gsub(/`/, "", ph);				\
			gsub(/^[ \t]+|[ \t]+$$/, "", ph);		\
			gsub(/`/, "", rs);				\
			gsub(/^[ \t]+|[ \t]+$$/, "", rs);		\
			if (ph == "_none_") ph = "";			\
			if (fl == "_none_") fl = "";			\
			if (c in d)					\
				no(FILENAME, c, "is published more"	\
					" than once");			\
			d[c] = key(ph, fl, rs); dn++; next };		\
		/^static ngx_http_status_def_t				\
			ngx_http_status_defs\[/ { t = 1; next };	\
		t && /^};/ { t = 0; next };				\
		!t { next };						\
		/^[ \t]*\{[ \t]*[0-9]/ { c = $$2 + 0; ph = "";		\
			if (!/ngx_null_string/) { ph = $$0;		\
				sub(/^[^"]*"/, "", ph);			\
				sub(/".*$$/, "", ph) };			\
			p = 1; next };					\
		p == 1 { fl = $$1; sub(/,$$/, "", fl);			\
			gsub(/NGX_HTTP_STATUS_/, "", fl);		\
			gsub(/\|/, ",", fl);				\
			if (fl == "0") fl = "";				\
			p = 2; next };					\
		p == 2 { rs = ""; if ($$1 != "NULL") { rs = $$0;	\
				sub(/^[^"]*"/, "", rs);			\
				sub(/".*$$/, "", rs) };			\
			p = 0; if (c == 0) next;			\
			if (!(c in d)) { no(FILENAME, c, "is"		\
					" described, and the reference"	\
					" publishes no such row");	\
				next };					\
			if (d[c] != key(ph, fl, rs))			\
				no(FILENAME, c, "is described as "	\
					key(ph, fl, rs)			\
					", published as " d[c]);	\
			delete d[c]; next };				\
		END { for (c in d) no(md, c, "is published, and"	\
					" the source describes no such"	\
					" row");			\
			if (!dn) { e++;					\
				print md ": publishes no table of"	\
					" rows" };			\
			if (e) exit 1 }' $(1) $(2)


ifeq ($(wildcard $(NGX_MAKEFILE)),)


# Nothing has been configured, so there is nothing to read and nothing to
# link.  No target but clean and metadata is reachable, neither of which needs
# a build, and none of the reading above is reached: every variable that reads
# is expanded only where it is used.

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

# The objects the recipe of the generated makefile hands the linker, in the
# order it hands them and every one of them: an object named there is an
# object the build was configured to compile, wherever it was written:
# under objs/src, under objs/addon for a static add-on, or beside objs/ as
# the generated ngx_modules.c is.  Only the object of the sole main() is left
# out, and check is what says that each of the rest is on disk, since linking
# against whichever of them happen to be there would test a smaller nginx
# than the build describes.

RECIPE_OBJS =	$(shell $(call ngx_link,objs))
LINK_OBJS =	$(filter-out $(MAIN_OBJ),$(RECIPE_OBJS))


test:		check metadata $(TEST_BIN)
	$(TEST_BIN)


# The driver reaches the registry through <ngx_http.h>, as every other file
# of the tree does, and it is the request that carries the bit the setter
# keeps, so each of those three headers is part of what is compiled here.
# The generated headers say what the build was configured to be, the
# generated makefile says with what it compiles, and this file says how, so
# a change to any of them is a reason to compile again as well.

$(TEST_OBJ):	$(TEST_SRC) $(TEST_MAKEFILE) $(NGX_MAKEFILE)		\
			objs/ngx_auto_config.h objs/ngx_auto_headers.h	\
			src/http/ngx_http.h src/http/ngx_http_status.h	\
			src/http/ngx_http_request.h | check
	mkdir -p objs/misc/status_test
	$(CC) -c $(CFLAGS) $(ALL_INCS)					\
		-o $(TEST_OBJ) $(TEST_SRC)


# Linked again when the driver object changes, when any object of the build
# changes, and when the recipe that names those objects or the flags that
# link them change, which is to say when the generated makefile or this file
# changes.

$(TEST_BIN):	$(TEST_OBJ) $(TEST_MAKEFILE) $(NGX_MAKEFILE)		\
			$(LINK_OBJS) | check
	$(LINK) -o $(TEST_BIN) $(TEST_OBJ) $(LINK_OBJS)			\
		$(LINK_TAIL)


# What the tests need of a build and may not find.  The recipe has to name
# the object of the sole main(), so that leaving that one out is what is
# being done; the object of the generated ngx_modules.c, without which
# ngx_modules[] is undefined; and the object of the registry, a build
# without which is a want of auto/modules rather than of anything here.
# Every object it names has to be on disk, and none of them older than what
# it is made of, which the generated makefile is asked rather than told.

check:
	@test -n "$(filter objs,$(ALL_INCS))" || {			\
		echo "$(NGX_MAKEFILE) has no -I objs among its include"	\
			"paths, so the generated headers of the build"	\
			"cannot be found" >&2;				\
		exit 1; }
	@test -n "$(RECIPE_OBJS)" || {					\
		echo "no recipe linking nginx could be read out of"	\
			"$(NGX_MAKEFILE), so what to link against is"	\
			"unknown" >&2;					\
		exit 1; }
	@test -n "$(filter $(MAIN_OBJ),$(RECIPE_OBJS))" || {		\
		echo "$(MAIN_OBJ) is not in the recipe linking nginx"	\
			"of $(NGX_MAKEFILE), so the object that defines"\
			"main() is not the one left out here" >&2;	\
		exit 1; }
	@test -n "$(filter $(MODULES_OBJ),$(RECIPE_OBJS))" || {		\
		echo "$(MODULES_OBJ) is not in the recipe linking"	\
			"nginx of $(NGX_MAKEFILE), so ngx_modules[]"	\
			"would be left undefined" >&2;			\
		exit 1; }
	@test -n "$(filter $(STATUS_OBJ),$(RECIPE_OBJS))" || {		\
		echo "$(STATUS_OBJ) is not in the recipe linking"	\
			"nginx of $(NGX_MAKEFILE): $(STATUS_SRC) has"	\
			"to be named in ngx_module_srcs in"		\
			"auto/modules" >&2;				\
		exit 1; }
	@for o in objs/nginx $(RECIPE_OBJS); do				\
		test -f $$o && continue;				\
		echo "$$o is not there, so the build is unfinished:"	\
			"run make first" >&2;				\
		exit 1;							\
	done
	@$(MAKE) -s -q -f $(NGX_MAKEFILE) objs/nginx || {		\
		echo "the objects of $(NGX_MAKEFILE) are older than"	\
			"what they are made of: run make first" >&2;	\
		exit 1; }


endif


# The flags and the provenance of every row of the registry, which no
# accessor of it publishes and so no run of the driver can reach.  Reads a
# source file and nothing of a build, so it is outside the reading above and
# answers on a tree that has never been configured.

metadata:
	@$(call ngx_metadata,$(STATUS_SRC)) >&2 || {			\
		echo "the rows above disagree with the class of their"	\
			"code or with what the registry documents" >&2;	\
		exit 1; }
	@$(call ngx_agree,$(STATUS_MD),$(STATUS_SRC)) >&2 || {		\
		echo "the rows above are described by the source and"	\
			"published by the reference differently" >&2;	\
		exit 1; }


# Only what the targets above write, and harmless where they have written
# nothing at all.

clean:
	rm -f objs/ngx_http_status_test
	rm -rf objs/misc/status_test
