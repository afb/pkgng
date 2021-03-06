/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <regex.h>

#include "pkgcli.h"

static const char * const scripts[] = {
	"+INSTALL",
	"+PRE_INSTALL",
	"+POST_INSTALL",
	"+POST_INSTALL",
	"+DEINSTALL",
	"+PRE_DEINSTALL",
	"+POST_DEINSTALL",
	"+UPGRADE",
	"+PRE_UPGRADE",
	"+POST_UPGRADE",
	"pkg-install",
	"pkg-pre-install",
	"pkg-post-install",
	"pkg-deinstall",
	"pkg-pre-deinstall",
	"pkg-post-deinstall",
	"pkg-upgrade",
	"pkg-pre-upgrade",
	"pkg-post-upgrade",
	NULL
};

void
usage_register(void)
{
	fprintf(stderr, "usage: pkg register [-Oldt] [-i <input-path>]"
	                " [-f <plist-file>] -m <metadatadir>\n");
	fprintf(stderr, "       pkg register [-Oldt] [-i <input_path>]"
		        " -M <manifest>\n\n");
	fprintf(stderr, "For more information see 'pkg help register'.\n");
}

int
exec_register(int argc, char **argv)
{
	struct pkg	*pkg = NULL;
	struct pkgdb	*db  = NULL;

	struct pkg_manifest_key *keys = NULL;

	regex_t		 preg;
	regmatch_t	 pmatch[2];

	char		*arch = NULL;
	char		 myarch[BUFSIZ];
	char		*www  = NULL;
	char		 fpath[MAXPATHLEN + 1];

	const char	*plist      = NULL;
	const char	*mdir       = NULL;
	const char	*mfile      = NULL;
	const char	*input_path = NULL;
	const char	*message;
	const char	*desc       = NULL;

	size_t		 size;

	bool		 developer;
	bool		 legacy        = false;
	bool		 old           = false;
	bool		 __unused metadata_only = false;
	bool		 testing_mode  = false;

	int		 ch;
	int		 i;
	int		 ret     = EPKG_OK;
	int		 retcode = EX_OK;

	pkg_config_bool(PKG_CONFIG_DEVELOPER_MODE, &developer);

	if (pkg_new(&pkg, PKG_INSTALLED) != EPKG_OK)
		err(EX_OSERR, "malloc");
	while ((ch = getopt(argc, argv, "df:i:lM:m:Ot")) != -1) {
		switch (ch) {
		case 'd':
			pkg_set(pkg, PKG_AUTOMATIC, (int64_t)true);
			break;
		case 'f':
			plist = optarg;
			break;
		case 'i':
			input_path = optarg;
			break;
		case 'l':
			legacy = true;
			break;
		case 'M':
			metadata_only = true;
			mfile = optarg;
			break;
		case 'm':
			mdir = optarg;
			break;
		case 'O':
			old = true;
			break;
		case 't':
			testing_mode = true;
			break;
		default:
			warnx("Unrecognised option -%c\n", ch);
			usage_register();
			return (EX_USAGE);
		}
	}

	if (!old) {
		retcode = pkgdb_access(PKGDB_MODE_READ  |
				       PKGDB_MODE_WRITE |
				       PKGDB_MODE_CREATE,
				       PKGDB_DB_LOCAL);
		if (retcode == EPKG_ENOACCESS) {
			warnx("Insufficient privilege to register packages");
			return (EX_NOPERM);
		} else if (retcode != EPKG_OK)
			return (EX_IOERR);
		else
			retcode = EX_OK;
	}

	/*
         * Ideally, the +MANIFEST should be all that is necessary,
	 * since it can contain all of the meta-data supplied by the
	 * other files mentioned below.  These are here for backwards
	 * compatibility with the way the ports tree works with
	 * pkg_tools.
	 * 
	 * The -M option specifies one manifest file to read the
	 * meta-data from, and overrides the use of legacy meta-data
	 * inputs.
	 *
	 * Dependencies, shlibs, files etc. may be derived by
	 * analysing the package files (maybe discovered as the
	 * content of the staging directory) unless -t (testing_mode)
	 * is used.
	 */

	if (mfile != NULL && mdir != NULL) {
		warnx("Cannot use both -m and -M together");
		usage_register();
		return (EX_USAGE);
	}


	if (mfile == NULL && mdir == NULL) {
		warnx("one of either -m or -M flags is required");
		usage_register();
		return (EX_USAGE);
	}

	if (mfile != NULL && plist != NULL) {
		warnx("-M incompatible with -f option");
		usage_register();
		return (EX_USAGE);
	}

	if (testing_mode && input_path != NULL) {
		warnx("-i incompatible with -t option");
		usage_register();
		return (EX_USAGE);
	}

	pkg_manifest_keys_new(&keys);

	if (mfile != NULL) {
		ret = pkg_load_manifest_file(pkg, mfile, keys);
		pkg_manifest_keys_free(keys);
		if (ret != EPKG_OK) 
			return (EX_IOERR);

	} else {
		snprintf(fpath, sizeof(fpath), "%s/+MANIFEST", mdir);
		ret = pkg_load_manifest_file(pkg, fpath, keys);
		pkg_manifest_keys_free(keys);
		if (ret != EPKG_OK)
			return (EX_IOERR);


		snprintf(fpath, sizeof(fpath), "%s/+DESC", mdir);
		pkg_set_from_file(pkg, PKG_DESC, fpath, false);

		snprintf(fpath, sizeof(fpath), "%s/+DISPLAY", mdir);
		if (access(fpath, F_OK) == 0)
			pkg_set_from_file(pkg, PKG_MESSAGE, fpath, false);

		snprintf(fpath, sizeof(fpath), "%s/+MTREE_DIRS", mdir);
		if (access(fpath, F_OK) == 0)
			pkg_set_from_file(pkg, PKG_MTREE, fpath, false);

		for (i = 0; scripts[i] != NULL; i++) {
			snprintf(fpath, sizeof(fpath), "%s/%s", mdir,
			    scripts[i]);
			if (access(fpath, F_OK) == 0)
				pkg_addscript_file(pkg, fpath);
		}

		if (www != NULL) {
			pkg_set(pkg, PKG_WWW, www);
			free(www);
		}

		pkg_get(pkg, PKG_WWW, &www);

		/* 
		 * if www is not given then try to determine it from
		 * description
		 */

		if (www == NULL) {
			pkg_get(pkg, PKG_DESC, &desc);
			regcomp(&preg, "^WWW:[[:space:]]*(.*)$",
			    REG_EXTENDED|REG_ICASE|REG_NEWLINE);
			if (regexec(&preg, desc, 2, pmatch, 0) == 0) {
				size = pmatch[1].rm_eo - pmatch[1].rm_so;
				www = strndup(&desc[pmatch[1].rm_so], size);
				pkg_set(pkg, PKG_WWW, www);
				free(www);
			} else {
				pkg_set(pkg, PKG_WWW, "UNKNOWN");
			}
			regfree(&preg);
		}

		if (plist != NULL)
			ret += ports_parse_plist(pkg, plist, input_path);
	}

	if (ret != EPKG_OK) {
		return (EX_IOERR);
	}


	if (!old && pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	/*
         * testing_mode allows updating the local package database
	 * without any check that the files etc. listed in the meta
	 * data actually exist on the system.  Inappropriate use of
	 * testing_mode can really screw things up.
	 */

	if (!testing_mode)
		pkg_analyse_files(db, pkg);

	pkg_get(pkg, PKG_ARCH, &arch);
	if (arch == NULL) {
		/*
		 * do not take the one from configuration on purpose
		 * but the real abi of the package.
		 */
		pkg_get_myarch(myarch, BUFSIZ);
		if (developer)
			pkg_suggest_arch(pkg, myarch, true);
		pkg_set(pkg, PKG_ARCH, myarch);
	} else {
		if (developer)
			pkg_suggest_arch(pkg, arch, false);
	}

	if (!testing_mode && input_path != NULL)
		pkg_copy_tree(pkg, input_path, "/");

	if (old) {
		if (pkg_register_old(pkg) != EPKG_OK)
			retcode = EX_SOFTWARE;
	} else {
		if (pkgdb_register_ports(db, pkg) != EPKG_OK)
			retcode = EX_SOFTWARE;
	}

	pkg_get(pkg, PKG_MESSAGE, &message);
	if (message != NULL && !legacy)
		printf("%s\n", message);

	if (!old)
		pkgdb_close(db);

	pkg_free(pkg);

	return (retcode);
}
