#include <stdio.h>
#include <stdlib.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/dpkgdb.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/init.h>
#include <apt-pkg/error.h>

int main(int argc, char *argv[]) {

    pkgInitConfig(*_config);
    pkgInitSystem(*_config, _system);

    OpTextProgress prog;
    pkgCacheFile cache;

    if (cache.Open(prog) == false) {
	_error->DumpErrors();
        exit(1);
    }

    fprintf(stderr, "%lud\n", cache->BrokenCount());
}
