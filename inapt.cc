#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <iostream>
#include <cstdio>
#include <fstream>
#include <set>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/dpkgdb.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/init.h>
#include <apt-pkg/error.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/acquire-item.h>

#include "inapt.h"
#include "util.h"
#include "acqprogress.h"

using namespace std;

char *prog = NULL;

static struct option opts[] = {
    { NULL, 0, NULL, '\0' },
};

bool InstallPackages(pkgCacheFile &Cache,bool ShwKept = false,bool Ask = true,
                     bool Safety = true)
{
   if (_config->FindB("APT::Get::Purge", false) == true)
   {
      pkgCache::PkgIterator I = Cache->PkgBegin();
      for (; I.end() == false; I++)
      {
         if (I.Purge() == false && Cache[I].Mode == pkgDepCache::ModeDelete)
            Cache->MarkDelete(I, true);
      }
   }

   if (Cache->BrokenCount() != 0)
   {
      return _error->Error("Internal error, InstallPackages was called with broken packages!");
   }

   if (Cache->DelCount() == 0 && Cache->InstCount() == 0 &&
       Cache->BadCount() == 0)
      return true;

   if (Cache->DelCount() != 0 && _config->FindB("APT::Get::Remove",true) == false)
      return _error->Error(("Packages need to be removed but remove is disabled."));

   pkgRecords Recs(Cache);
   if (_error->PendingError() == true)
      return false;

   FileFd Lock;
   if (_config->FindB("Debug::NoLocking",false) == false &&
       _config->FindB("APT::Get::Print-URIs") == false)
   {
      Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
      if (_error->PendingError() == true)
         return _error->Error(("Unable to lock the download directory"));
   }

   unsigned int width = 80;
   AcqTextStatus status (width, 0);
   pkgAcquire Fetcher (&status);

   pkgSourceList List;
   if (List.ReadMainList() == false)
      return _error->Error(("The list of sources could not be read."));

   SPtr<pkgPackageManager> PM= _system->CreatePM(Cache);
   if (PM->GetArchives(&Fetcher, &List, &Recs) == false ||
       _error->PendingError() == true)
      return false;

   if (_error->PendingError() == true)
      return false;

   while (1)
   {
      bool Transient = false;
      if (_config->FindB("APT::Get::Download",true) == false)
      {
         for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); I < Fetcher.ItemsEnd();)
         {
            if ((*I)->Local == true)
            {
               I++;
               continue;
            }

            // Close the item and check if it was found in cache
            (*I)->Finished();
            if ((*I)->Complete == false)
               Transient = true;

            // Clear it out of the fetch list
            delete *I;
            I = Fetcher.ItemsBegin();
         }
      }

      if (Fetcher.Run() == pkgAcquire::Failed)
         return false;

      // Print out errors
      bool Failed = false;
      for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
      {
         if ((*I)->Status == pkgAcquire::Item::StatDone &&
             (*I)->Complete == true)
            continue;

         if ((*I)->Status == pkgAcquire::Item::StatIdle)
         {
            Transient = true;
            // Failed = true;
            continue;
         }

         fprintf(stderr,("Failed to fetch %s  %s\n"),(*I)->DescURI().c_str(),
                 (*I)->ErrorText.c_str());
         Failed = true;
      }

      /* If we are in no download mode and missing files and there were
         'failures' then the user must specify -m. Furthermore, there
         is no such thing as a transient error in no-download mode! */
      if (Transient == true &&
          _config->FindB("APT::Get::Download",true) == false)
      {
         Transient = false;
         Failed = true;
      }

      if (_config->FindB("APT::Get::Download-Only",false) == true)
      {
         if (Failed == true && _config->FindB("APT::Get::Fix-Missing",false) == false)
            return _error->Error(("Some files failed to download"));
         //c1out << _("Download complete and in download only mode") << endl;
         return true;
      }

      if (Failed == true && _config->FindB("APT::Get::Fix-Missing",false) == false)
      {
         return _error->Error(("Unable to fetch some archives, maybe run apt-get update or try with --fix-missing?"));
      }

      if (Transient == true && Failed == true)
         return _error->Error(("--fix-missing and media swapping is not currently supported"));

      // Try to deal with missing package files
      if (Failed == true && PM->FixMissing() == false)
      {
         cerr << ("Unable to correct missing packages.") << endl;
         return _error->Error(("Aborting install."));
      }

      _system->UnLock();
      int status_fd = _config->FindI("APT::Status-Fd",-1);
      pkgPackageManager::OrderResult Res = PM->DoInstall(status_fd);
      if (Res == pkgPackageManager::Failed || _error->PendingError() == true)
         return false;
      if (Res == pkgPackageManager::Completed)
         return true;

      // Reload the fetcher object and loop again for media swapping
      Fetcher.Shutdown();
      if (PM->GetArchives(&Fetcher,&List,&Recs) == false)
         return false;

      _system->Lock();
   }
}

static void usage() {
    fprintf(stderr, "Usage: %s [filename]\n", prog);
    exit(2);
}

static void eval_block(inapt_block *block, set<string> *defines, std::vector<inapt_action *> *final_actions) {
    for (vector<inapt_action *>::iterator i = block->actions.begin(); i < block->actions.end(); i++)
        final_actions->push_back(*i);

    for (vector<inapt_conditional *>::iterator i = block->children.begin(); i < block->children.end(); i++) {
        if (defines->find((*i)->condition) != defines->end())
            eval_block((*i)->then_block, defines, final_actions);
        else
            eval_block((*i)->else_block, defines, final_actions);
    }
}

static void exec_actions(std::vector<inapt_action *> *final_actions) {

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

    for (vector<inapt_action *>::iterator i = final_actions->begin(); i < final_actions->end(); i++) {
        pkgCache::PkgIterator pkg = cache->FindPkg((*i)->package);
        if (pkg.end())
            fatal("%s:%d: No such package: %s", (*i)->filename, (*i)->linenum, (*i)->package);
        (*i)->obj = &pkg;
    }

    for (vector<inapt_action *>::iterator i = final_actions->begin(); i < final_actions->end(); i++) {
        pkgCache::PkgIterator j = *(pkgCache::PkgIterator *)(*i)->obj;
        switch ((*i)->action) {
            case inapt_action::INSTALL:
                if (!cachef[j].InstallVer || cachef[j].Delete()) {
                    printf("preinstall %s %s:%d\n", (*i)->package, (*i)->filename, (*i)->linenum);
                    DCache->MarkInstall(j, true);
                }
                break;
            case inapt_action::REMOVE:
                break;
            default:
                fatal("uninitialized action");
        }
    }

    for (vector<inapt_action *>::iterator i = final_actions->begin(); i < final_actions->end(); i++) {
        pkgCache::PkgIterator j = *(pkgCache::PkgIterator *)(*i)->obj;
        switch ((*i)->action) {
            case inapt_action::INSTALL:
                if (!cachef[j].InstallVer || cachef[j].Delete()) {
                    printf("install %s %s:%d\n", (*i)->package, (*i)->filename, (*i)->linenum);
                    DCache->MarkInstall(j, false);
                } else {
                    printf("install %s %s:%d\n", (*i)->package, (*i)->filename, (*i)->linenum);
                }
                break;
            case inapt_action::REMOVE:
                if (cachef[j].InstallVer || cachef[j].Delete()) {
                    printf("remove %s %s:%d\n", (*i)->package, (*i)->filename, (*i)->linenum);
                    DCache->MarkDelete(j, false);
                }
                break;
            default:
                fatal("uninitialized action");
        }
    }

    fprintf(stderr, "\n");
    fprintf(stderr, "nondownloadable packages:\n");
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
       if (i.CurrentVer() && !i.CurrentVer().Downloadable()) {
	       fprintf(stderr, "%s ", i.Name());
	       fprintf(stderr, "%s\n", DCache->GetCandidateVer(i).VerStr());
       }
    }

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

    for (vector<inapt_action *>::iterator i = final_actions->begin(); i < final_actions->end(); i++)
	    fix.Protect(cache->FindPkg((*i)->package));
    for (vector<inapt_action *>::iterator i = final_actions->begin(); i < final_actions->end(); i++)
	    fix.Protect(cache->FindPkg((*i)->package));
    fix.Resolve();

    fprintf(stderr, "\n");
    fprintf(stderr, "inst %lu del %lu keep %lu broken %lu bad %lu\n",
		    DCache->InstCount(), DCache->DelCount(), DCache->KeepCount(),
		    DCache->BrokenCount(), DCache->BadCount());
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
       if ((*DCache)[i].Install())
         fprintf(stderr, "inst %s\n", i.Name());
       if ((*DCache)[i].Delete())
         fprintf(stderr, "del %s\n", i.Name());
       if ((*DCache)[i].InstBroken())
         fprintf(stderr, "instbroken %s\n", i.Name());
       if ((*DCache)[i].NowBroken())
         fprintf(stderr, "nowbroken %s\n", i.Name());
    }

    fprintf(stderr, "\n");

    InstallPackages(cachef);
}

int main(int argc, char *argv[]) {
    int opt;
    char *filename = NULL;

    set<string> defines;

    prog = xstrdup(basename(argv[0]));
    while ((opt = getopt_long(argc, argv, "D:U:", opts, NULL)) != -1) {
        switch (opt) {
            case 'D':
                defines.insert(optarg);
                break;
            case 'U':
                defines.erase(optarg);
                break;
            case '?':
                usage();
                break;
            default:
                fatal("error parsing arguments");
        }
    }

    if (argc - optind == 1)
        filename = argv[optind++];
    else if (argc - optind > 0)
        usage();

    fprintf(stderr, "defines: ");
    for (set<string>::iterator i = defines.begin(); i != defines.end(); i++)
        fprintf(stderr, "%s  ", i->c_str());
    fprintf(stderr, "\n");

    inapt_block context;

    parser(filename, &context);

    vector<inapt_action *> final_actions;
    eval_block(&context, &defines, &final_actions);
    exec_actions(&final_actions);
}
