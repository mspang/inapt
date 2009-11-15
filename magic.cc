#include <stdio.h>
#include <stdlib.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/dpkgdb.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/init.h>
#include <apt-pkg/error.h>
#include <apt-pkg/algorithms.h>

int main(int argc, char *argv[]) {

    pkgInitConfig(*_config);
    pkgInitSystem(*_config, _system);

     _config->Set("Debug::pkgProblemResolver", true);

    OpTextProgress prog;
    pkgCacheFile cachef;

    if (cachef.Open(prog) == false) {
	_error->DumpErrors();
        exit(1);
    }

    pkgCache *cache = cachef;
    pkgDepCache *DCache = cachef;

    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
       if (i.CurrentVer() && !i.CurrentVer().Downloadable()) {
	       fprintf(stderr, "%s ", i.Name());
	       fprintf(stderr, "%s\n", DCache->GetCandidateVer(i).VerStr());
       }
    }

    DCache->MarkInstall(cache->FindPkg("zsh"), true);
    DCache->MarkInstall(cache->FindPkg("ssmtp"), true);
    DCache->MarkInstall(cache->FindPkg("gnome"), true);
    DCache->MarkInstall(cache->FindPkg("postfix"), true);
    DCache->MarkDelete(cache->FindPkg("gnome-games"), false);

    fprintf(stderr, "\n");
    fprintf(stderr, "inst %lu del %lu keep %lu broken %lu bad %lu\n",
		    DCache->InstCount(), DCache->DelCount(), DCache->KeepCount(),
		    DCache->BrokenCount(), DCache->BadCount());

    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
       if ((*DCache)[i].Install())
         fprintf(stderr, "inst %s\n", i.Name());
       if ((*DCache)[i].InstBroken())
         fprintf(stderr, "instbroken %s\n", i.Name());
       if ((*DCache)[i].NowBroken())
         fprintf(stderr, "nowbroken %s\n", i.Name());
    }

    fprintf(stderr, "\n");

    pkgProblemResolver fix (DCache);
    fix.Protect(cache->FindPkg("ssmtp"));
    fix.Protect(cache->FindPkg("gnome-games"));
    fix.Resolve();

    fprintf(stderr, "\n");
    fprintf(stderr, "inst %lu del %lu keep %lu broken %lu bad %lu\n",
		    DCache->InstCount(), DCache->DelCount(), DCache->KeepCount(),
		    DCache->BrokenCount(), DCache->BadCount());
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
       if ((*DCache)[i].Install())
         fprintf(stderr, "inst %s\n", i.Name());
       if ((*DCache)[i].InstBroken())
         fprintf(stderr, "instbroken %s\n", i.Name());
       if ((*DCache)[i].NowBroken())
         fprintf(stderr, "nowbroken %s\n", i.Name());
    }

    fprintf(stderr, "\n");

}
