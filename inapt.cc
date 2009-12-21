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
    { "auto-remove", 0, NULL, 'g' },
    { "simulate", 0, NULL, 's' },
    { "purge", 0, NULL, 'u' },
    { NULL, 0, NULL, '\0' },
};

bool run_install(pkgCacheFile &cache) {
   if (_config->FindB("Inapt::Purge", false))
      for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++)
         if (!i.Purge() && cache[i].Mode == pkgDepCache::ModeDelete)
            cache->MarkDelete(i, true);

   if (cache->BrokenCount())
       fatal("broken packages during install");

   if (!cache->DelCount() && !cache->InstCount() && !cache->BadCount())
      return true;

   pkgRecords Recs (cache);
   if (_error->PendingError())
      return false;

   FileFd Lock;
   Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
   if (_error->PendingError())
       return _error->Error("Unable to lock the download directory");

   unsigned int width = 80;
   AcqTextStatus status (width, 0);
   pkgAcquire Fetcher (&status);

   pkgSourceList List;
   if (List.ReadMainList() == false)
      return _error->Error("The list of sources could not be read");

   SPtr<pkgPackageManager> PM = _system->CreatePM(cache);
   if (PM->GetArchives(&Fetcher, &List, &Recs) == false ||
       _error->PendingError())
      return false;

  if (Fetcher.Run() == pkgAcquire::Failed)
     return false;

  bool Failed = false;
  for (pkgAcquire::ItemIterator i = Fetcher.ItemsBegin(); i != Fetcher.ItemsEnd(); i++) {
     if ((*i)->Status != pkgAcquire::Item::StatDone || (*i)->Complete != true) {
         fprintf(stderr,("Failed to fetch %s  %s\n"),(*i)->DescURI().c_str(),
                 (*i)->ErrorText.c_str());
         Failed = true;
     }
  }

  if (Failed)
     return _error->Error("Unable to fetch some archives");

  _system->UnLock();

  pkgPackageManager::OrderResult Res = PM->DoInstall(-1);
  if (Res == pkgPackageManager::Completed)
     return true;

  return false;
}

void run_autoremove(pkgCacheFile &cache) {
    bool purge = _config->FindB("Inapt::Purge", false);

    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
        if (cache[i].Garbage) {
            debug("autoremove: %s", i.Name());
            cache->MarkDelete(i, purge);
        }
    }

    if (cache->BrokenCount())
        fatal("automatic removal broke packages");
}

static void usage() {
    fprintf(stderr, "Usage: %s [filename..]\n", prog);
    exit(2);
}

static bool test_macro(const char *macro, std::set<std::string> *defines) {
    return (*macro != '!' && defines->find(macro) != defines->end())
            || (*macro == '!' && defines->find(macro + 1) == defines->end());
}

static pkgCache::PkgIterator eval_pkg(inapt_package *package, pkgCacheFile &cache) {
    pkgCache::PkgIterator pkg;

    for (std::vector<std::string>::iterator i = package->alternates.begin(); i != package->alternates.end(); i++) {
        pkgCache::PkgIterator tmp = cache->FindPkg(*i);

        /* no such package */
        if (tmp.end())
            continue;

        /* real package */
        if (cache[tmp].CandidateVer) {
            pkg = tmp;
            break;
        }

        /* virtual package */
        if (tmp->ProvidesList) {
            if (!tmp.ProvidesList()->NextProvides) {
                pkgCache::PkgIterator provide = tmp.ProvidesList().OwnerPkg();
                if (package->action == inapt_action::INSTALL) {
                    debug("selecting %s instead of %s", provide.Name(), tmp.Name());
                    pkg = provide;
                    break;
                } else {
                    debug("will not remove %s instead of virtual package %s", provide.Name(), tmp.Name());
                }
            } else {
                debug("%s is a virtual package", tmp.Name());
            }
        } else {
            debug("%s is a virtual packages with no provides", tmp.Name());
        }
    }

    if (pkg.end()) {
        if (package->alternates.size() == 1) {
            _error->Error("%s:%d: No such package: %s", package->filename, package->linenum, package->alternates[0].c_str());
        } else {
            std::vector<std::string>::iterator i = package->alternates.begin();
            std::string message = *(i++);
            while (i != package->alternates.end()) {
                message.append(", ");
                message.append(*(i++));
            }
            _error->Error("%s:%d: No alternative available: %s", package->filename, package->linenum, message.c_str());
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

static void show_breakage(pkgCacheFile &cache) {
    fprintf(stderr, "fatal: Unable to solve dependencies\n");
    fprintf(stderr, "The following packages are broken:");
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++)
        if (cache[i].NowBroken() || cache[i].InstBroken())
            fprintf(stderr, " %s", i.Name());
    fprintf(stderr, "\n");
    exit(1);
}

static void exec_actions(std::vector<inapt_package *> *final_actions) {
    int marked = 0;
    bool purge = _config->FindB("Inapt::Purge", false);

    pkgInitConfig(*_config);
    pkgInitSystem(*_config, _system);

//     _config->Set("Debug::pkgProblemResolver", true);
//     _config->Set("Debug::pkgAutoRemove", true);

    OpTextProgress prog;
    pkgCacheFile cache;

    if (cache.Open(prog) == false)
        return;

    pkgDepCache::ActionGroup group (cache);

    for (vector<inapt_package *>::iterator i = final_actions->begin(); i != final_actions->end(); i++)
        (*i)->pkg = eval_pkg(*i, cache);

    if (_error->PendingError())
        return;

    for (vector<inapt_package *>::iterator i = final_actions->begin(); i < final_actions->end(); i++) {
        pkgCache::PkgIterator k = (*i)->pkg;
        switch ((*i)->action) {
            case inapt_action::INSTALL:
                if (!k.CurrentVer() || cache[k].Delete()) {
                    debug("install %s %s:%d", (*i)->pkg.Name(), (*i)->filename, (*i)->linenum);
                    cache->MarkInstall(k, true);
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
                if ((!k.CurrentVer() && !cache[k].Install()) || cache[k].Delete()) {
                    debug("install %s %s:%d", (*i)->pkg.Name(), (*i)->filename, (*i)->linenum);
                    cache->MarkInstall(k, false);
                }
                if (cache[k].Flags & pkgCache::Flag::Auto) {
                    debug("marking %s as manually installed", (*i)->pkg.Name());
                    cache->MarkAuto(k, false);
                    marked++;
                }
                break;
            case inapt_action::REMOVE:
                if ((k.CurrentVer() && !cache[k].Delete()) || cache[k].Install())
                    debug("remove %s %s:%d", (*i)->pkg.Name(), (*i)->filename, (*i)->linenum);

                /* always mark so purge works */
                cache->MarkDelete(k, purge);
                break;
            default:
                fatal("uninitialized action");
        }
    }

    if (_error->PendingError())
        return;

    dump_nondownloadable(cache);
    dump_actions(cache);

    if (cache->BrokenCount()) {
        pkgProblemResolver fix (cache);
        for (vector<inapt_package *>::iterator i = final_actions->begin(); i < final_actions->end(); i++)
            fix.Protect((*i)->pkg);
        fix.Resolve();

        if (cache->BrokenCount())
            show_breakage(cache);
    }

    if (_config->FindB("Inapt::AutomaticRemove", false)) {
        cache->MarkAndSweep();
        run_autoremove(cache);
    }

    if (_config->FindB("Inapt::Simulate", false)) {
        pkgSimulate PM (cache);
        PM.DoInstall(-1);
        return;
    }

    run_install(cache);
    if (_error->PendingError())
        return;

    if (marked) {
        if (_config->FindB("Inapt::Simulate", false)) {
            debug("marked %d packages", marked);
        } else {
            debug("marked %d packages, writing state file", marked);
            cache->writeStateFile(NULL);
        }
    }
}

static void debug_profiles(std::set<std::string> *defines) {
    std::string profiles = "profiles:";

    for (std::set<std::string>::iterator i = defines->begin(); i != defines->end(); i++) {
        profiles.append(" ");
        profiles.append(*i);
    }

    debug("%s", profiles.c_str());
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
    while ((opt = getopt_long(argc, argv, "p:sdg", opts, NULL)) != -1) {
        switch (opt) {
            case 'p':
                defines.insert(optarg);
                break;
            case '?':
                usage();
                break;
            case 'g':
                _config->Set("Inapt::AutomaticRemove", true);
                break;
            case 's':
                _config->Set("Inapt::Simulate", true);
                break;
            case 'u':
                _config->Set("Inapt::Purge", true);
                break;
            case 'd':
                debug_enabled = true;
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

    if (_error->PendingError()) {
	_error->DumpErrors();
        exit(1);
    }
}
