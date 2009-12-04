#include <vector>

struct inapt_action {
    const char *package;
    enum action_t { INSTALL, REMOVE, UNSET } action;
    const char *filename;
    int linenum;
    void *obj;
};

void parser(const char *filename, std::vector<inapt_action> *actions);
