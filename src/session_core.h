#ifndef NIXBENCH_SESSION_CORE_H
#define NIXBENCH_SESSION_CORE_H

enum {
    NB_SESSION_CORE_PROTOCOL_DESCRIPTOR = 3
};

/* Run the ordinary-user desktop core on one inherited helper connection. */
int nb_session_core_run(int protocol_descriptor,
                        const char *initial_application_path);

#endif
