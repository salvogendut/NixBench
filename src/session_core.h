#ifndef NIXBENCH_SESSION_CORE_H
#define NIXBENCH_SESSION_CORE_H

enum {
    NB_SESSION_CORE_PROTOCOL_DESCRIPTOR = 3
};

/* Run the ordinary-user desktop core on one inherited helper connection.
 * A NULL initial_application_path starts with an empty desktop.
 * A NULL runtime_directory_path creates an internally owned test directory;
 * production supplies the directory owned by the cleanup sentinel.
 * A NULL user_config_path selects HOME/.nixbenchrc; tests may inject an
 * absolute, unprivileged path. */
int nb_session_core_run(int protocol_descriptor,
                        const char *initial_application_path,
                        const char *runtime_directory_path,
                        const char *core_program_path,
                        const char *user_config_path);

#endif
