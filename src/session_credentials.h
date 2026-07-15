#ifndef NIXBENCH_SESSION_CREDENTIALS_H
#define NIXBENCH_SESSION_CREDENTIALS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

enum {
    NB_SESSION_CREDENTIALS_USER_CAPACITY = 256,
    NB_SESSION_CREDENTIALS_PATH_CAPACITY = 4096,
    NB_SESSION_CREDENTIALS_ERROR_CAPACITY = 256,
    NB_SESSION_CREDENTIALS_IPC_FD = 3,
    NB_SESSION_CREDENTIALS_SETUP_EXIT = 126,
    NB_SESSION_CREDENTIALS_EXEC_EXIT = 127
};

/*
 * A complete, copied account identity. No field points into getenv(3) or the
 * password database's transient storage.
 */
struct nb_session_credentials {
    uid_t uid;
    gid_t gid;
    char user[NB_SESSION_CREDENTIALS_USER_CAPACITY];
    char home[NB_SESSION_CREDENTIALS_PATH_CAPACITY];
    char shell[NB_SESSION_CREDENTIALS_PATH_CAPACITY];
};

struct nb_session_sudo_identity {
    const char *uid_text;
    const char *gid_text;
    const char *user;
};

/* Borrowed strings returned by one account lookup. */
struct nb_session_account {
    uid_t uid;
    gid_t gid;
    const char *name;
    const char *home;
    const char *shell;
};

/*
 * Lookup callbacks return zero on success or a positive errno value. They
 * make all validation and mismatch handling testable without root access.
 * Returned strings need remain valid only until the next lookup callback.
 */
struct nb_session_account_lookup {
    int (*by_name)(void *opaque,
                   const char *name,
                   struct nb_session_account *account);
    int (*by_uid)(void *opaque,
                  uid_t uid,
                  struct nb_session_account *account);
};

bool nb_session_credentials_resolve_with_lookup(
    const struct nb_session_sudo_identity *sudo_identity,
    const struct nb_session_account_lookup *lookup,
    void *lookup_context,
    struct nb_session_credentials *credentials,
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY]);

/* Resolve SUDO_UID, SUDO_GID, and SUDO_USER against the system passwd DB. */
bool nb_session_credentials_resolve_sudo(
    struct nb_session_credentials *credentials,
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY]);

/*
 * Run only in a freshly forked, single-threaded child. This function creates
 * a new session, establishes fd 3 as the sole inherited session channel,
 * irreversibly changes to credentials, and execs an absolute core path with a
 * minimal environment. It reports failure on standard error and _exit(2)s;
 * it never returns to privileged caller code.
 */
_Noreturn void nb_session_credentials_drop_and_exec(
    const struct nb_session_credentials *credentials,
    int ipc_fd,
    const char *core_path,
    char *const core_argv[]);

#endif
