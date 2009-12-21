#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/utsname.h>
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

char *prog = NULL;

static struct option opts[] = {
    { "auto-remove", 0, NULL, 'z' },
    { "simulate", 0, NULL, 's' },
    { "purge", 0, NULL, 'u' },
    { NULL, 0, NULL, '\0' },
};

class AwesomeRootSetFunc : public pkgDepCache::InRootSetFunc {
    std::set<std::string> root;

    public:
        AwesomeRootSetFunc(std::vector<inapt_package *> *final_actions) {
            for (vector<inapt_package *>::iterator i = final_actions->begin(); i != final_actions->end(); i++)
                if ((*i)->action == inapt_action::INSTALL)
                    root.insert((*i)->pkg.Name());
        }
        bool InRootSet(const pkgCache::PkgIterator &pkg) {
            return root.find(pkg.Name()) != root.end();
        }
};

bool run_install(pkgCacheFile &Cache,bool ShwKept = false,bool Ask = true,
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

   if (Cache->BrokenCount())
       fatal("broken packages during install");

   if (!Cache->DelCount() && !Cache->InstCount() && !Cache->BadCount())
      return true;

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
         std::cerr << ("Unable to correct missing packages.") << std::endl;
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

void run_autoremove(pkgCacheFile &cache)
{
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
        if (cache[i].Garbage) {
            debug("garbage: %s", i.Name());
            cache->MarkDelete(i, 0);
        }
    }

    if (cache->BrokenCount())
        fatal("ogawd");
}

static void usage() {
    fprintf(stderr, "Usage: %s [filename..]\n", prog);
    exit(2);
}

static bool test_macro(const char *macro, std::set<std::string> *defines) {
    return (*macro != '!' && defines->find(macro) != defines->end())
            || (*macro == '!' && defines->find(macro + 1) == defines->end());
}

static pkgCache::PkgIterator eval_pkg(inapt_package *package, pkgCacheFile &cachef) {
    pkgCache::PkgIterator pkg;

    pkgCache *cache = cachef;

    if (!pkg.end()) fatal("omg"); /* TODO */

    for (std::vector<std::string>::iterator i = package->alternates.begin(); i != package->alternates.end(); i++) {
        pkg = cache->FindPkg(*i);

        /* no such package */
        if (pkg.end())
            continue;

        /* real package */
        if (cachef[pkg].CandidateVer)
            break;

        /* virtual package */
        if (pkg->ProvidesList) {
            if (!pkg.ProvidesList()->NextProvides) {
                pkgCache::PkgIterator tmp = pkg.ProvidesList().OwnerPkg();
                if (package->action == inapt_action::INSTALL) {
                    debug("selecting %s instead of %s", tmp.Name(), pkg.Name());
                    pkg = tmp;
                    break;
                } else {
                    debug("will not remove %s instead of virtual package %s", tmp.Name(), pkg.Name());
                }
            } else {
                debug("%s is a virtual package", pkg.Name());
            }
        } else {
            debug("%s is a virtual packages with no provides", pkg.Name());
        }
    }

    if (pkg.end()) {
        /* todo: report all errors at the end */
        if (package->alternates.size() == 1) {
            fatal("%s:%d: No such package: %s", package->filename, package->linenum, package->alternates[0].c_str());
        } else {
            std::vector<std::string>::iterator i = package->alternates.begin();
            std::string message = *(i++);
            while (i != package->alternates.end()) {
                message.append(", ");
                message.append(*(i++));
            }
            fatal("%s:%d: No alternative available: %s", package->filename, package->linenum, message.c_str());
        }
    }

    return pkg;
}

static bool test_macros(vector<std::string> *macros, std::set<std::string> *defines) {
    bool ok = true;
    for (vector<std::string>::iterator j = macros->begin(); j < macros->end(); j++) {
        if (!test_macro((*j).c_str(), defines)) {
            ok = false;
            break;
        }
    }
    return ok;
}

static void eval_action(inapt_action *action, std::set<std::string> *defines, std::vector<inapt_package *> *final_actions) {
    for (vector<inapt_package *>::iterator i = action->packages.begin(); i < action->packages.end(); i++) {
        if (test_macros(&(*i)->predicates, defines))
            final_actions->push_back(*i);
    }
}

static void eval_block(inapt_block *block, std::set<std::string> *defines, std::vector<inapt_package *> *final_actions) {
    if (!block)
        return;

    for (vector<inapt_action *>::iterator i = block->actions.begin(); i < block->actions.end(); i++)
        if (test_macros(&(*i)->predicates, defines))
            eval_action(*i, defines, final_actions);

    for (vector<inapt_conditional *>::iterator i = block->children.begin(); i < block->children.end(); i++) {
        if (test_macro((*i)->condition, defines))
            eval_block((*i)->then_block, defines, final_actions);
        else
            eval_block((*i)->else_block, defines, final_actions);
    }
}

static void eval_profiles(inapt_block *block, std::set<std::string> *defines) {
    if (!block)
        return;

    for (vector<inapt_profiles *>::iterator i = block->profiles.begin(); i < block->profiles.end(); i++)
        if (test_macros(&(*i)->predicates, defines))
            for (vector<std::string>::iterator j = (*i)->profiles.begin(); j != (*i)->profiles.end(); j++)
                defines->insert(*j);

    for (vector<inapt_conditional *>::iterator i = block->children.begin(); i < block->children.end(); i++) {
        if (test_macro((*i)->condition, defines))
            eval_profiles((*i)->then_block, defines);
        else
            eval_profiles((*i)->else_block, defines);
    }
}

static void dump_nondownloadable(pkgCacheFile &cache) {
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++)
       if (i.CurrentVer() && !i.CurrentVer().Downloadable())
           debug("package %s version %s is not downloadable", i.Name(), i.CurrentVer().VerStr());
}

static void dump_actions(pkgCacheFile &cache) {
    debug("inst %lu del %lu keep %lu broken %lu bad %lu",
		    cache->InstCount(), cache->DelCount(), cache->KeepCount(),
		    cache->BrokenCount(), cache->BadCount());
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
       if (cache[i].Install())
         debug("installing %s", i.Name());
       if (cache[i].Delete())
         debug("removing %s", i.Name());
       if (cache[i].InstBroken())
         debug("install broken %s", i.Name());
       if (cache[i].NowBroken())
         debug("now broken %s", i.Name());
    }
}

static void exec_actions(std::vector<inapt_package *> *final_actions) {
    int marked = 0;

    pkgInitConfig(*_config);
    pkgInitSystem(*_config, _system);

//     _config->Set("Debug::pkgProblemResolver", true);
//     _config->Set("Debug::pkgAutoRemove", true);

    OpTextProgress prog;
    pkgCacheFile cachef;

    if (cachef.Open(prog) == false) {
	_error->DumpErrors();
        exit(1);
    }

    pkgDepCache::ActionGroup group (*cachef);

    for (vector<inapt_package *>::iterator i = final_actions->begin(); i != final_actions->end(); i++)
        (*i)->pkg = eval_pkg(*i, cachef);

    for (vector<inapt_package *>::iterator i = final_actions->begin(); i < final_actions->end(); i++) {
        pkgCache::PkgIterator k = (*i)->pkg;
        switch ((*i)->action) {
            case inapt_action::INSTALL:
                if (!k.CurrentVer() || cachef[k].Delete()) {
                    debug("install %s %s:%d", (*i)->pkg.Name(), (*i)->filename, (*i)->linenum);
                    cachef->MarkInstall(k, true);
                }
                break;
            case inapt_action::REMOVE:
                break;
            default:
                fatal("uninitialized action");
        }
    }

    for (vector<inapt_package *>::iterator i = final_actions->begin(); i < final_actions->end(); i++) {
        pkgCache::PkgIterator k = (*i)->pkg;
        switch ((*i)->action) {
            case inapt_action::INSTALL:
                if ((!k.CurrentVer() && !cachef[k].Install()) || cachef[k].Delete()) {
                    debug("install %s %s:%d", (*i)->pkg.Name(), (*i)->filename, (*i)->linenum);
                    cachef->MarkInstall(k, false);
                }
                if (cachef[k].Flags & pkgCache::Flag::Auto) {
                    marked++;
                    debug("marking %s as manually installed", (*i)->pkg.Name());
                    cachef->MarkAuto(k, false);
                }
                break;
            case inapt_action::REMOVE:
                if ((k.CurrentVer() && !cachef[k].Delete()) || cachef[k].Install()) {
                    debug("remove %s %s:%d", (*i)->pkg.Name(), (*i)->filename, (*i)->linenum);
                    cachef->MarkDelete(k, false);
                }
                break;
            default:
                fatal("uninitialized action");
        }
    }

    dump_nondownloadable(cachef);
    dump_actions(cachef);

    pkgProblemResolver fix (cachef);
    for (vector<inapt_package *>::iterator i = final_actions->begin(); i < final_actions->end(); i++)
        fix.Protect((*i)->pkg);
    fix.Resolve();

    dump_actions(cachef);

    if (_config->FindB("Inapt::AutomaticRemove", false)) {
        cachef->MarkAndSweep();
        run_autoremove(cachef);
    }

    if (!run_install(cachef)) {
	_error->DumpErrors();
        fatal("errors");
    }

    if (marked) {
        debug("marked %d packages, writing state file", marked);
        cachef->writeStateFile(NULL);
    }

    /*
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++)
        cachef->MarkAuto(i, true);

    AwesomeRootSetFunc root (final_actions);

    cachef->MarkAndSweep(root);
    fprintf(stderr, "\n");
    fprintf(stderr, "garbage packages:\n");
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
       if (i.CurrentVer() && cachef[i].Garbage) {
	       fprintf(stderr, "%s ", i.Name());
	       fprintf(stderr, "%s\n", cachef->GetCandidateVer(i).VerStr());
       }
    }
    */
}

static void debug_profiles(std::set<std::string> *defines) {
    fprintf(stderr, "debug: defines: ");
    for (std::set<std::string>::iterator i = defines->begin(); i != defines->end(); i++)
        fprintf(stderr, "%s ", i->c_str());
    fprintf(stderr, "\n");
}

static void auto_profiles(std::set<std::string> *defines) {
    struct utsname uts;
    if (uname(&uts))
        fatalpe("uname");
    defines->insert(uts.nodename);
}

int main(int argc, char *argv[]) {
    int opt;

    std::set<std::string> defines;

    prog = xstrdup(basename(argv[0]));
    while ((opt = getopt_long(argc, argv, "p:", opts, NULL)) != -1) {
        switch (opt) {
            case 'p':
                defines.insert(optarg);
                break;
            case '?':
                usage();
                break;
            case 'z':
                _config->Set("Inapt::AutomaticRemove", true);
                break;
            case 's':
                _config->Set("Inapt::Simulate", true);
                break;
            case 'u':
                _config->Set("Inapt::Purge", true);
                break;
            default:
                fatal("error parsing arguments");
        }
    }

    int num_files = argc - optind;

    inapt_block context;
    std::vector<inapt_package *> final_actions;

    if (!num_files)
        parser(NULL, &context);

    while (num_files--)
        parser(argv[optind++], &context);

    auto_profiles(&defines);
    eval_profiles(&context, &defines);
    debug_profiles(&defines);
    eval_block(&context, &defines, &final_actions);
    exec_actions(&final_actions);

    /* TODO: remove this */
    if (_error->PendingError()) {
        warn("uncaught errors:");
	_error->DumpErrors();
        exit(1);
    }
}
