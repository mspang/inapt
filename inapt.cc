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
#include <apt-pkg/progress.h>
#include <apt-pkg/init.h>
#include <apt-pkg/error.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/acquire-item.h>

#include "inapt.h"
#include "util.h"
#include "contrib/acqprogress.h"

char *prog = NULL;

static struct option opts[] = {
    { "help", 0, NULL, 'h' },
    { "simulate", 0, NULL, 's' },
    { "profile", 0, NULL, 'p' },
    { "update", 0, NULL, 'l' },
    { "upgrade", 0, NULL, 'u' },
    { "check", 0, NULL, 'c' },
    { "purge", 0, NULL, '!' },
    { "clean", 0, NULL, 'e' },
    { "option", 0, NULL, 'o' },
    { "strict", 0, NULL, 't' },
    { NULL, 0, NULL, '\0' },
};

static bool run_install(pkgCacheFile &cache) {
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

   pkgAcquire Fetcher;

   unsigned int width = 80;
   AcqTextStatus status (width, 0);
   Fetcher.Setup(&status);

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
     if ((*i)->Status != pkgAcquire::Item::StatDone || (*i)->Complete != true)
         Failed = true;
  }

  if (Failed)
     return _error->Error("Unable to fetch some archives");

  _system->UnLock();

  pkgPackageManager::OrderResult Res = PM->DoInstall(-1);
  if (Res == pkgPackageManager::Completed)
     return true;

  return false;
}

static void run_autoremove(pkgCacheFile &cache) {
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
    fprintf(stderr, "Usage: %s [options] [filename..]\n", prog);
    exit(2);
}

static bool test_profile(const char *profile, std::set<std::string> *profiles) {
    return (*profile != '!' && profiles->find(profile) != profiles->end())
            || (*profile == '!' && profiles->find(profile + 1) == profiles->end());
}

static bool test_anyprofile(std::string &profile, std::set<std::string> *profiles) {
    char *s = xstrdup(profile.c_str());
    const char *c = strtok(s, "/");

    if (test_profile(c, profiles)) {
        free(s);
        return true;
    }

    while ((c = strtok(NULL, "/")) != NULL) {
        if (test_profile(c, profiles)) {
            free(s);
            return true;
        }
    }

    free(s);
    return false;
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
	    if (_config->FindB("Inapt::Strict", false))
		    _error->Error("%s:%d: No such package: %s", package->filename, package->linenum, package->alternates[0].c_str());
	    else
		    _error->Warning("%s:%d: No such package: %s", package->filename, package->linenum, package->alternates[0].c_str());
        } else {
            std::vector<std::string>::iterator i = package->alternates.begin();
            std::string message = *(i++);
            while (i != package->alternates.end()) {
                message.append(", ").append(*(i++));
            }
	    if (_config->FindB("Inapt::Strict", false))
		    _error->Error("%s:%d: No alternative available: %s", package->filename, package->linenum, message.c_str());
	    else
		    _error->Warning("%s:%d: No alternative available: %s", package->filename, package->linenum, message.c_str());
        }
    }

    return pkg;
}

static bool test_profiles(vector<std::string> *test_profiles, std::set<std::string> *profiles) {
    bool ok = true;
    for (vector<std::string>::iterator j = test_profiles->begin(); j < test_profiles->end(); j++) {
        if (!test_anyprofile(*j, profiles)) {
            ok = false;
            break;
        }
    }
    return ok;
}

static void eval_action(inapt_action *action, std::set<std::string> *profiles, std::vector<inapt_package *> *final_actions) {
    for (vector<inapt_package *>::iterator i = action->packages.begin(); i < action->packages.end(); i++) {
        if (test_profiles(&(*i)->predicates, profiles))
            final_actions->push_back(*i);
    }
}

static void eval_block(inapt_block *block, std::set<std::string> *profiles, std::vector<inapt_package *> *final_actions) {
    if (!block)
        return;

    for (vector<inapt_action *>::iterator i = block->actions.begin(); i < block->actions.end(); i++)
        if (test_profiles(&(*i)->predicates, profiles))
            eval_action(*i, profiles, final_actions);

    for (vector<inapt_conditional *>::iterator i = block->children.begin(); i < block->children.end(); i++) {
        if (test_profiles(&(*i)->predicates, profiles))
            eval_block((*i)->then_block, profiles, final_actions);
        else
            eval_block((*i)->else_block, profiles, final_actions);
    }
}

static void eval_profiles(inapt_block *block, std::set<std::string> *profiles) {
    if (!block)
        return;

    for (vector<inapt_profiles *>::iterator i = block->profiles.begin(); i < block->profiles.end(); i++)
        if (test_profiles(&(*i)->predicates, profiles))
            for (vector<std::string>::iterator j = (*i)->profiles.begin(); j != (*i)->profiles.end(); j++)
                profiles->insert(*j);

    for (vector<inapt_conditional *>::iterator i = block->children.begin(); i < block->children.end(); i++) {
        if (test_profiles(&(*i)->predicates, profiles))
            eval_profiles((*i)->then_block, profiles);
        else
            eval_profiles((*i)->else_block, profiles);
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

static bool sanity_check(std::vector<inapt_package *> *final_actions, pkgCacheFile &cache) {
    bool okay = true;
    std::map<std::string, inapt_package *> packages;

    for (vector<inapt_package *>::iterator i = final_actions->begin(); i != final_actions->end(); i++) {
	if ((*i)->pkg.end())
		continue;
        if (packages.find((*i)->pkg.Name()) != packages.end()) {
            inapt_package *first = packages[(*i)->pkg.Name()];
            inapt_package *current = *i;
            _error->Error("Multiple directives for package %s at %s:%d and %s:%d",
                    (*i)->pkg.Name(), first->filename, first->linenum, current->filename, current->linenum);
            okay = false;
            continue;
        }
        packages[(*i)->pkg.Name()] = *i;
    }

    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++) {
        if (cache[i].Delete() && (i->Flags & pkgCache::Flag::Essential || i->Flags & pkgCache::Flag::Important)) {
            _error->Error("Removing essential package %s", i.Name());
            okay = false;
        }
    }

    return okay;
}

static void show_breakage(pkgCacheFile &cache) {
    std::string broken;
    for (pkgCache::PkgIterator i = cache->PkgBegin(); !i.end(); i++)
        if (cache[i].NowBroken() || cache[i].InstBroken())
            broken.append(" ").append(i.Name());

    _error->Error("Broken packages:%s", broken.c_str());
}

static void exec_actions(std::vector<inapt_package *> *final_actions) {
    int marked = 0;
    bool purge = _config->FindB("Inapt::Purge", false);

    pkgInitConfig(*_config);
    pkgInitSystem(*_config, _system);

    OpTextProgress prog;
    pkgCacheFile cache;

    if (cache.Open(&prog, true) == false)
        return;

    pkgDepCache::ActionGroup group (cache);

    for (vector<inapt_package *>::iterator i = final_actions->begin(); i != final_actions->end(); i++)
        (*i)->pkg = eval_pkg(*i, cache);

    if (_error->PendingError())
        return;
    _error->DumpErrors();

    // preliminary loop (auto-installs, includes recommends - could do this manually)
    for (vector<inapt_package *>::iterator i = final_actions->begin(); i < final_actions->end(); i++) {
        pkgCache::PkgIterator k = (*i)->pkg;
	if (k.end())
		continue;
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

    // secondary loop (removes package and reinstalls auto-removed packages)
    for (vector<inapt_package *>::iterator i = final_actions->begin(); i < final_actions->end(); i++) {
        pkgCache::PkgIterator k = (*i)->pkg;
	if (k.end())
		continue;
        switch ((*i)->action) {
            case inapt_action::INSTALL:
                if ((!k.CurrentVer() && !cache[k].Install()) || cache[k].Delete()) {
                    debug("force install %s %s:%d", (*i)->pkg.Name(), (*i)->filename, (*i)->linenum);
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

        if (cache->BrokenCount()) {
            show_breakage(cache);
            return;
        }
    }

    cache->MarkAndSweep();
    run_autoremove(cache);

    if (!sanity_check(final_actions, cache))
        return;

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

static void debug_profiles(std::set<std::string> *profiles) {
    std::string s = "profiles:";

    for (std::set<std::string>::iterator i = profiles->begin(); i != profiles->end(); i++) {
        s.append(" ");
        s.append(*i);
    }

    debug("%s", s.c_str());
}

static void auto_profiles(std::set<std::string> *profiles) {
    struct utsname uts;
    if (uname(&uts))
        fatalpe("uname");
    profiles->insert(uts.nodename);
}

static void set_option(char *opt) {
    char *eq = strchr(opt, '=');
    if (!eq)
        fatal("invalid syntax for '%s': must be <option>=<value>", opt);

    std::string option (opt, eq - opt);
    std::string value (eq + 1);

    _config->Set(option, value);
}

int main(int argc, char *argv[]) {
    int opt;

    std::set<std::string> profiles;

    prog = xstrdup(basename(argv[0]));
    while ((opt = getopt_long(argc, argv, "?hp:slucedo:", opts, NULL)) != -1) {
        switch (opt) {
            case '?':
            case 'h':
                usage();
                break;
            case 'p':
                profiles.insert(optarg);
                break;
            case 's':
                _config->Set("Inapt::Simulate", true);
                break;
            case '!':
                _config->Set("Inapt::Purge", true);
                break;
            case 'l':
                _config->Set("Inapt::Update", true);
                break;
            case 'u':
                _config->Set("Inapt::Upgrade", true);
                break;
            case 'c':
                _config->Set("Inapt::Check", true);
                break;
            case 'e':
                _config->Set("Inapt::Clean", true);
                break;
            case 't':
                _config->Set("Inapt::Strict", true);
                break;
            case 'd':
                debug_level++;
                break;
            case 'o':
                set_option(optarg);
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

    auto_profiles(&profiles);
    eval_profiles(&context, &profiles);
    debug_profiles(&profiles);
    eval_block(&context, &profiles, &final_actions);
    exec_actions(&final_actions);

    if (_error->PendingError()) {
        _error->DumpErrors();
        exit(1);
    }

    return 0;
}
