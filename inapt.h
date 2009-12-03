#include <vector>

struct inapt_action {
    const char *package;
    enum action_t { INSTALL, REMOVE } action;
    const char *filename;
    int linenum;
};

void scanner(std::vector<inapt_action> *actions);
