include $(top_srcdir)/misc/Makefile.common

hackersguide_sgml = hackersguide.sgml \
		    intro.sgml \
		    library.sgml \
		    overview.sgml \
		    internals.sgml \
		    stream.sgml \
		    output.sgml

docs_DOCS = hackersguide.html architecture.png library.png overlays.png

EXTRA_DIST = README $(hackersguide_sgml) $(docs_DOCS) \
	architecture.fig library.fig overlays.fig 

docdir = $(prefix)/share/doc/xine/hackersguide

install-data-local:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
		destdir=$(DESTDIR)$(docdir); \
		name=`echo $$doc` \
		dir=$$destdir; \
		$(mkinstalldirs) $$dir; \
		$(INSTALL_DATA) $$doc $$dir/$$name; \
		echo "installing $$doc as $$dir/$$name"; \
	done

uninstall-local:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
		destdir=$(DESTDIR)$(docdir); \
		name=`echo $$doc` \
		dir=$$destdir; \
		rm -f $$dir/$$name; \
		echo "removing $$dir/$$name" ; \
	done

docs: $(docs_DOCS)

hackersguide.html: $(hackersguide_sgml)
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b onehtml hackersguide.sgml; \
	fi

%.png: %.fig
	@if test "$(FIG2DEV)" != "no"; then \
	  $(FIG2DEV) -L png -S 4 $< $@; \
	fi
