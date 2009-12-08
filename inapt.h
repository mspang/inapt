#include <vector>

struct inapt_action {
    const char *package;
    enum action_t { INSTALL, REMOVE, UNSET } action;
    const char *filename;
    int linenum;
    void *obj;
};

struct inapt_context {
    const char *condition;
    std::vector<inapt_action *> actions;
    std::vector<inapt_context *> children;
};

void parser(const char *filename, inapt_context *context);
