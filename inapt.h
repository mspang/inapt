#include <vector>
#include <apt-pkg/pkgcache.h>

struct inapt_conditional;

struct inapt_action {
    const char *package;
    enum action_t { INSTALL, REMOVE, UNSET } action;
    const char *filename;
    int linenum;
    pkgCache::PkgIterator pkg;
};

struct inapt_block {
    std::vector<inapt_action *> actions;
    std::vector<inapt_conditional *> children;
};

struct inapt_conditional {
    const char *condition;
    struct inapt_block *then_block;
    struct inapt_block *else_block;
};

void parser(const char *filename, inapt_block *context);
