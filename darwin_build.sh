# Requirements:
# - libutil (from Apple)
# - libarchive (from GitHub)
# - libfetch (from FreeBSD)
# - libsbuf (from SourceForge)
# - liblzma (from tukaani.org)
CC=clang bsdmake -DDARWIN -DNO_WERROR -DNO_LIBJAIL -DNO_BSDXML
