#include <vector>
#include <apt-pkg/pkgcache.h>

struct inapt_conditional;
struct inapt_package;

struct inapt_action {
    enum action_t { INSTALL, REMOVE } action;
    std::vector<std::string> predicates;
    std::vector<inapt_package *> packages;
};

struct inapt_package {
    enum inapt_action::action_t action;
    std::vector<std::string> alternates;
    std::vector<std::string> predicates;
    pkgCache::PkgIterator pkg;
    const char *filename;
    int linenum;
};

struct inapt_profiles {
    std::vector<std::string> predicates;
    std::vector<std::string> profiles;
};

struct inapt_block {
    std::vector<inapt_action *> actions;
    std::vector<inapt_conditional *> children;
    std::vector<inapt_profiles *> profiles;
};

struct inapt_conditional {
    std::vector<std::string> predicates;
    struct inapt_block *then_block;
    struct inapt_block *else_block;
};

void parser(const char *filename, inapt_block *context);
