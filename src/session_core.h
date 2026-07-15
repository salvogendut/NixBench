#ifndef NIXBENCH_SESSION_CORE_H
#define NIXBENCH_SESSION_CORE_H

enum {
    NB_SESSION_CORE_PROTOCOL_DESCRIPTOR = 3
};

/* Run the ordinary-user desktop core on one inherited helper connection.
 * A NULL runtime_directory_path creates an internally owned test directory;
 * production supplies the directory owned by the cleanup sentinel. */
int nb_session_core_run(int protocol_descriptor,
                        const char *initial_application_path,
                        const char *runtime_directory_path);

#endif
