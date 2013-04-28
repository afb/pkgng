#include <unistd.h>

#include "eaccess.h"

int
eaccess(const char *path, int mode)
{
	uid_t	ruid, euid;
	gid_t	rgid, egid;
	int		rv;
	int		done = 0;

	ruid = getuid();
	euid = geteuid();
	rgid = getgid();
	egid = getegid();

	if (!setregid(egid, rgid)) {
		if (!setreuid(euid, ruid)) {
			rv = access(path, mode);
			done = 1;
			setreuid(ruid, euid);
		}
		setregid(rgid, egid);
	}

	/* fall back */
	if (!done)
		rv = access(path, mode);

	return(rv);
}
