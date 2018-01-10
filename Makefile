      GRDIR = /usr/local/gr
     CONFIG = xft=yes
       DIRS = lib/gr lib/gr3
ALL_DISTROS = centos centos6 debian suse
ifeq ($(DISTROS),all)
	override DISTROS = $(ALL_DISTROS)
endif

UNAME := $(shell uname)

default: pre-check Makedefs

pre-check:
	@lib/Precheck "${GRDIR}"  || \
	( echo "FATAL: Source and target directory are identical"; exit 1 )
	@make `uname`

Makedefs:
	@lib/Preflight $(CONFIG) >Makedefs

Linux: all
Darwin: all

all:
	@for d in $(DIRS); do make -C $$d GRDIR=$(GRDIR); done
ifeq ($(UNAME), Darwin)
	(env CC=cc xcodebuild -project lib/gks/quartz/GKSTerm.xcodeproj)
endif

install: default
	@for d in $(DIRS); do make -C $$d GRDIR=$(GRDIR) install; done
ifeq ($(UNAME), Darwin)
	@if [ ! -d $(DESTDIR)$(GRDIR)/Applications ]; then \
	mkdir -m 755 $(DESTDIR)$(GRDIR)/Applications; fi
	@ditto lib/gks/quartz/build/Release/GKSTerm.app \
	$(DESTDIR)$(GRDIR)/Applications/GKSTerm.app 
endif

clean:
	rm -f Makedefs
	@for d in $(DIRS) 3rdparty; do make -C $$d clean; done
ifeq ($(UNAME), Darwin)
	(env CC=cc xcodebuild -project lib/gks/quartz/GKSTerm.xcodeproj clean)
endif
	cp -p lib/gks/quartz/project.pbxproj lib/gks/quartz/GKSTerm.xcodeproj/
	rm -f gr.pkg

realclean: clean
	make -C 3rdparty realclean
	rm -rf build
	find packaging -type f \( -name '*.deb' -o -name '*.rpm' \) -exec rm \{\} \;
	rm -rf tmp

self:
	sh 3rdparty/makeself.sh

osxpkg:
	mkdir -p tmp/bin tmp/gr
	rsync -a /usr/local/gr/ tmp/gr/
	ln -s ../gr/bin/gr tmp/bin/gr
	ln -s ../gr/Applications/glgr.app/Contents/MacOS/glgr tmp/bin/glgr
	ln -s ../gr/Applications/gksqt.app/Contents/MacOS/gksqt tmp/bin/gksqt
	sudo chown -R -h root:wheel tmp/
	pkgbuild --identifier de.fz-juelich.gr --root tmp --install-location /usr/local --ownership preserve gr.pkg
	sudo rm -rf tmp

linuxpackages: DESTDIR=$(shell pwd)/tmp
linuxpackages: GRDIR=/opt/gr
linuxpackages:
	echo $(DISTROS)
	@which fpm >/dev/null 2>&1 || \
	( echo "FATAL: fpm could not be found in PATH.\n       Visit https://github.com/jordansissel/fpm for more information on fpm."; exit 1 )
	mkdir -p $(DESTDIR)$(GRDIR)
	env DESTDIR=$(DESTDIR) GRDIR=$(GRDIR) sh 3rdparty/makeself.sh
ifndef DISTROS
	@./packaging/create_package.sh
else
	@for DISTRO in $(DISTROS); do \
		./packaging/create_package.sh "$${DISTRO}"; \
	done
endif
	rm -rf $(DESTDIR)

mirror:
	(cd ../gr-github && git fetch && git push)
