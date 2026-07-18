#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "session_credentials.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif

enum {
    NB_SESSION_PASSWD_BUFFER_DEFAULT = 16384,
    NB_SESSION_PASSWD_BUFFER_MAXIMUM = 1024 * 1024,
    NB_SESSION_GROUP_COUNT_MAXIMUM = 65536,
    NB_SESSION_NOFILE_MAXIMUM = 4096
};

static const char safe_path[] = "/usr/pkg/bin:/usr/bin:/bin";

static void set_error(
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY],
    const char *format,
    ...)
{
    va_list arguments;

    if (error == NULL) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error,
                    NB_SESSION_CREDENTIALS_ERROR_CAPACITY,
                    format,
                    arguments);
    va_end(arguments);
}

static bool copy_text(char *destination,
                      size_t destination_size,
                      const char *source)
{
    const int length = source != NULL
                           ? snprintf(destination,
                                      destination_size,
                                      "%s",
                                      source)
                           : -1;

    return length >= 0 && (size_t)length < destination_size;
}

static bool parse_identifier(const char *text,
                             uintmax_t *value,
                             const char *label,
                             char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY])
{
    const char *character;
    char *end = NULL;
    uintmax_t parsed;

    if (text == NULL || text[0] == '\0') {
        set_error(error, "%s is missing", label);
        return false;
    }
    for (character = text; *character != '\0'; ++character) {
        if (*character < '0' || *character > '9') {
            set_error(error, "%s must be an unsigned decimal integer", label);
            return false;
        }
    }
    errno = 0;
    parsed = strtoumax(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        set_error(error, "%s is outside the supported numeric range", label);
        return false;
    }
    *value = parsed;
    return true;
}

static bool account_strings_are_valid(
    const struct nb_session_account *account)
{
    return account != NULL && account->name != NULL &&
           account->name[0] != '\0' && account->home != NULL &&
           account->home[0] == '/' && account->shell != NULL &&
           account->shell[0] == '/';
}

static bool copy_account(struct nb_session_credentials *destination,
                         const struct nb_session_account *source,
                         char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY])
{
    if (!account_strings_are_valid(source)) {
        set_error(error, "Password database returned an invalid account");
        return false;
    }
    memset(destination, 0, sizeof(*destination));
    destination->uid = source->uid;
    destination->gid = source->gid;
    if (!copy_text(destination->user,
                   sizeof(destination->user),
                   source->name) ||
        !copy_text(destination->home,
                   sizeof(destination->home),
                   source->home) ||
        !copy_text(destination->shell,
                   sizeof(destination->shell),
                   source->shell)) {
        memset(destination, 0, sizeof(*destination));
        set_error(error, "Password database account fields are too long");
        return false;
    }
    return true;
}

static bool accounts_match(
    const struct nb_session_credentials *expected,
    const struct nb_session_account *actual)
{
    return account_strings_are_valid(actual) &&
           actual->uid == expected->uid && actual->gid == expected->gid &&
           strcmp(actual->name, expected->user) == 0 &&
           strcmp(actual->home, expected->home) == 0 &&
           strcmp(actual->shell, expected->shell) == 0;
}

bool nb_session_credentials_resolve_with_lookup(
    const struct nb_session_sudo_identity *sudo_identity,
    const struct nb_session_account_lookup *lookup,
    void *lookup_context,
    struct nb_session_credentials *credentials,
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY])
{
    struct nb_session_account account;
    struct nb_session_credentials candidate;
    uintmax_t uid_value;
    uintmax_t gid_value;
    uid_t uid;
    gid_t gid;
    int result;

    if (credentials != NULL) {
        memset(credentials, 0, sizeof(*credentials));
    }
    if (error != NULL) {
        error[0] = '\0';
    }
    if (sudo_identity == NULL || lookup == NULL ||
        lookup->by_name == NULL || lookup->by_uid == NULL ||
        credentials == NULL) {
        set_error(error, "Invalid session credential arguments");
        return false;
    }
    if (sudo_identity->user == NULL || sudo_identity->user[0] == '\0') {
        set_error(error, "SUDO_USER is missing");
        return false;
    }
    if (!parse_identifier(sudo_identity->uid_text,
                          &uid_value,
                          "SUDO_UID",
                          error) ||
        !parse_identifier(sudo_identity->gid_text,
                          &gid_value,
                          "SUDO_GID",
                          error)) {
        return false;
    }
    uid = (uid_t)uid_value;
    gid = (gid_t)gid_value;
    if ((uintmax_t)uid != uid_value || uid == (uid_t)-1 ||
        (uintmax_t)gid != gid_value || gid == (gid_t)-1) {
        set_error(error, "Sudo identity does not fit native uid/gid types");
        return false;
    }
    if (uid == (uid_t)0) {
        set_error(error, "Refusing to run the desktop core as root");
        return false;
    }

    memset(&account, 0, sizeof(account));
    result = lookup->by_name(lookup_context,
                             sudo_identity->user,
                             &account);
    if (result != 0) {
        set_error(error,
                  "Could not resolve SUDO_USER %s: %s",
                  sudo_identity->user,
                  strerror(result > 0 ? result : EIO));
        return false;
    }
    if (account.uid != uid || account.gid != gid ||
        strcmp(account.name != NULL ? account.name : "",
               sudo_identity->user) != 0) {
        set_error(error,
                  "SUDO_UID/SUDO_GID/SUDO_USER do not name one account");
        return false;
    }
    if (!copy_account(&candidate, &account, error)) {
        return false;
    }

    memset(&account, 0, sizeof(account));
    result = lookup->by_uid(lookup_context, uid, &account);
    if (result != 0) {
        set_error(error,
                  "Could not resolve SUDO_UID %ju: %s",
                  uid_value,
                  strerror(result > 0 ? result : EIO));
        return false;
    }
    if (!accounts_match(&candidate, &account)) {
        set_error(error,
                  "Name and uid password-database lookups disagree");
        return false;
    }

    *credentials = candidate;
    return true;
}

struct system_lookup_context {
    struct passwd record;
    char *buffer;
    size_t buffer_size;
};

static bool grow_passwd_buffer(struct system_lookup_context *context)
{
    size_t next_size;
    char *next;

    if (context->buffer_size >= NB_SESSION_PASSWD_BUFFER_MAXIMUM) {
        return false;
    }
    next_size = context->buffer_size == 0
                    ? NB_SESSION_PASSWD_BUFFER_DEFAULT
                    : context->buffer_size * 2U;
    if (next_size > NB_SESSION_PASSWD_BUFFER_MAXIMUM) {
        next_size = NB_SESSION_PASSWD_BUFFER_MAXIMUM;
    }
    next = realloc(context->buffer, next_size);
    if (next == NULL) {
        return false;
    }
    context->buffer = next;
    context->buffer_size = next_size;
    return true;
}

static void account_from_passwd(const struct passwd *record,
                                struct nb_session_account *account)
{
    account->uid = record->pw_uid;
    account->gid = record->pw_gid;
    account->name = record->pw_name;
    account->home = record->pw_dir;
    account->shell = record->pw_shell;
}

static int system_lookup_by_name(void *opaque,
                                 const char *name,
                                 struct nb_session_account *account)
{
    struct system_lookup_context *context = opaque;
    struct passwd *found = NULL;
    int result;

    for (;;) {
        if (context->buffer == NULL && !grow_passwd_buffer(context)) {
            return ENOMEM;
        }
        result = getpwnam_r(name,
                            &context->record,
                            context->buffer,
                            context->buffer_size,
                            &found);
        if (result != ERANGE) {
            break;
        }
        if (!grow_passwd_buffer(context)) {
            return ERANGE;
        }
    }
    if (result != 0) {
        return result;
    }
    if (found == NULL) {
        return ENOENT;
    }
    account_from_passwd(found, account);
    return 0;
}

static int system_lookup_by_uid(void *opaque,
                                uid_t uid,
                                struct nb_session_account *account)
{
    struct system_lookup_context *context = opaque;
    struct passwd *found = NULL;
    int result;

    for (;;) {
        if (context->buffer == NULL && !grow_passwd_buffer(context)) {
            return ENOMEM;
        }
        result = getpwuid_r(uid,
                            &context->record,
                            context->buffer,
                            context->buffer_size,
                            &found);
        if (result != ERANGE) {
            break;
        }
        if (!grow_passwd_buffer(context)) {
            return ERANGE;
        }
    }
    if (result != 0) {
        return result;
    }
    if (found == NULL) {
        return ENOENT;
    }
    account_from_passwd(found, account);
    return 0;
}

bool nb_session_credentials_resolve_sudo(
    struct nb_session_credentials *credentials,
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY])
{
    struct system_lookup_context context;
    const struct nb_session_account_lookup lookup = {
        .by_name = system_lookup_by_name,
        .by_uid = system_lookup_by_uid
    };
    const struct nb_session_sudo_identity sudo_identity = {
        .uid_text = getenv("SUDO_UID"),
        .gid_text = getenv("SUDO_GID"),
        .user = getenv("SUDO_USER")
    };
    bool success;

    memset(&context, 0, sizeof(context));
    success = nb_session_credentials_resolve_with_lookup(
        &sudo_identity,
        &lookup,
        &context,
        credentials,
        error);
    free(context.buffer);
    return success;
}

static _Noreturn void child_failure(int exit_status,
                                    const char *operation,
                                    int system_error)
{
    char message[512];
    int length;

    if (system_error != 0) {
        length = snprintf(message,
                          sizeof(message),
                          "nixbench session child: %s: %s\n",
                          operation,
                          strerror(system_error));
    } else {
        length = snprintf(message,
                          sizeof(message),
                          "nixbench session child: %s\n",
                          operation);
    }
    if (length > 0) {
        const size_t size = (size_t)length < sizeof(message)
                                ? (size_t)length
                                : sizeof(message) - 1U;
        const char *bytes = message;
        size_t remaining = size;

        while (remaining != 0U) {
            const ssize_t written = write(STDERR_FILENO, bytes, remaining);

            if (written > 0) {
                bytes += (size_t)written;
                remaining -= (size_t)written;
            } else if (written < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
    }
    _exit(exit_status);
}

static bool credential_record_is_valid(
    const struct nb_session_credentials *credentials)
{
    return credentials != NULL && credentials->uid != (uid_t)0 &&
           credentials->uid != (uid_t)-1 &&
           credentials->gid != (gid_t)-1 && credentials->user[0] != '\0' &&
           memchr(credentials->user,
                  '\0',
                  sizeof(credentials->user)) != NULL &&
           credentials->home[0] == '/' &&
           memchr(credentials->home,
                  '\0',
                  sizeof(credentials->home)) != NULL &&
           credentials->shell[0] == '/' &&
           memchr(credentials->shell,
                  '\0',
                  sizeof(credentials->shell)) != NULL;
}

static bool reset_one_signal(int signal_number)
{
    struct sigaction action;

    if (signal_number == SIGKILL || signal_number == SIGSTOP) {
        return true;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask) != 0) {
        return false;
    }
    if (sigaction(signal_number, &action, NULL) == 0) {
        return true;
    }
    return errno == EINVAL;
}

static bool reset_signals(void)
{
    sigset_t all_signals;
    int maximum_signal = 64;
    int signal_number;

#if defined(NSIG)
    maximum_signal = NSIG - 1;
#elif defined(SIGRTMAX)
    maximum_signal = SIGRTMAX;
#endif
    if (sigfillset(&all_signals) != 0 ||
        sigprocmask(SIG_BLOCK, &all_signals, NULL) != 0) {
        return false;
    }
    for (signal_number = 1;
         signal_number <= maximum_signal;
         ++signal_number) {
        if (!reset_one_signal(signal_number)) {
            return false;
        }
    }
    return true;
}

static bool unblock_signals_for_exec(void)
{
    sigset_t no_signals;

    return sigemptyset(&no_signals) == 0 &&
           sigprocmask(SIG_SETMASK, &no_signals, NULL) == 0;
}

static bool lower_limit(int resource, rlim_t maximum)
{
    struct rlimit limit;

#if defined(__linux__)
    const __rlimit_resource_t native_resource =
        (__rlimit_resource_t)resource;
#else
    const int native_resource = resource;
#endif

    if (getrlimit(native_resource, &limit) != 0) {
        return false;
    }
    if (limit.rlim_max == RLIM_INFINITY || limit.rlim_max > maximum) {
        limit.rlim_max = maximum;
    }
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur > limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max;
    }
    return setrlimit(native_resource, &limit) == 0;
}

static bool establish_safe_limits(void)
{
    if (!lower_limit(RLIMIT_CORE, (rlim_t)0) ||
        !lower_limit(RLIMIT_NOFILE, (rlim_t)NB_SESSION_NOFILE_MAXIMUM)) {
        return false;
    }
#if defined(RLIMIT_MEMLOCK)
    if (!lower_limit(RLIMIT_MEMLOCK, (rlim_t)0)) {
        return false;
    }
#endif
#if defined(__linux__) && defined(RLIMIT_RTPRIO)
    if (!lower_limit(RLIMIT_RTPRIO, (rlim_t)0)) {
        return false;
    }
#endif
#if defined(__linux__) && defined(RLIMIT_NICE)
    if (!lower_limit(RLIMIT_NICE, (rlim_t)0)) {
        return false;
    }
#endif
    return true;
}

static bool set_descriptor_cloexec(int descriptor, bool enabled)
{
    const int flags = fcntl(descriptor, F_GETFD);

    return flags >= 0 &&
           fcntl(descriptor,
                 F_SETFD,
                 enabled ? flags | FD_CLOEXEC : flags & ~FD_CLOEXEC) == 0;
}

static bool place_ipc_descriptor(int ipc_fd)
{
    if (ipc_fd < NB_SESSION_CREDENTIALS_IPC_FD) {
        errno = EBADF;
        return false;
    }
    if (ipc_fd != NB_SESSION_CREDENTIALS_IPC_FD &&
        dup2(ipc_fd, NB_SESSION_CREDENTIALS_IPC_FD) < 0) {
        return false;
    }
    if (ipc_fd > NB_SESSION_CREDENTIALS_IPC_FD) {
        (void)close(ipc_fd);
    }
    return set_descriptor_cloexec(NB_SESSION_CREDENTIALS_IPC_FD, true);
}

static bool ensure_standard_descriptors(void)
{
    int descriptor;

    for (descriptor = STDIN_FILENO;
         descriptor <= STDERR_FILENO;
         ++descriptor) {
        if (fcntl(descriptor, F_GETFD) < 0) {
            int null_fd;

            if (errno != EBADF) {
                return false;
            }
            null_fd = open("/dev/null", O_RDWR | O_NOCTTY);
            if (null_fd < 0) {
                return false;
            }
            if (null_fd != descriptor) {
                if (dup2(null_fd, descriptor) < 0) {
                    const int saved_error = errno;

                    (void)close(null_fd);
                    errno = saved_error;
                    return false;
                }
                (void)close(null_fd);
            }
        }
        if (!set_descriptor_cloexec(descriptor, false)) {
            return false;
        }
    }
    return true;
}

bool nb_session_credentials_prepare_parent_stdio(void)
{
    return ensure_standard_descriptors();
}

static void close_unexpected_descriptors(void)
{
#if defined(__NetBSD__) || defined(__linux__)
    closefrom(NB_SESSION_CREDENTIALS_IPC_FD + 1);
#else
    struct rlimit limit;
    rlim_t upper = 65536;
    int descriptor;

    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 &&
        limit.rlim_max != RLIM_INFINITY) {
        upper = limit.rlim_max;
    }
    if (upper > (rlim_t)INT_MAX) {
        upper = (rlim_t)INT_MAX;
    }
    for (descriptor = NB_SESSION_CREDENTIALS_IPC_FD + 1;
         (rlim_t)descriptor < upper;
         ++descriptor) {
        (void)close(descriptor);
    }
#endif
}

static bool identities_match(const struct nb_session_credentials *credentials)
{
#if defined(__linux__)
    uid_t real_uid;
    uid_t effective_uid;
    uid_t saved_uid;
    gid_t real_gid;
    gid_t effective_gid;
    gid_t saved_gid;

    return getresuid(&real_uid, &effective_uid, &saved_uid) == 0 &&
           getresgid(&real_gid, &effective_gid, &saved_gid) == 0 &&
           real_uid == credentials->uid &&
           effective_uid == credentials->uid &&
           saved_uid == credentials->uid && real_gid == credentials->gid &&
           effective_gid == credentials->gid &&
           saved_gid == credentials->gid;
#else
    /* NetBSD 10 has no getresuid/getresgid API. setuid by effective root is
     * specified to replace the saved uid; the caller additionally performs
     * an active seteuid(0) reacquisition test immediately after this check. */
    return getuid() == credentials->uid &&
           geteuid() == credentials->uid && getgid() == credentials->gid &&
           getegid() == credentials->gid;
#endif
}

struct nb_session_group_list {
    gid_t *groups;
    int count;
};

static bool resize_group_list(struct nb_session_group_list *list,
                              int count)
{
    gid_t *groups;

    if (count <= 0 || count > NB_SESSION_GROUP_COUNT_MAXIMUM ||
        (size_t)count > SIZE_MAX / sizeof(*groups)) {
        errno = EOVERFLOW;
        return false;
    }
    groups = realloc(list->groups, (size_t)count * sizeof(*groups));
    if (groups == NULL) {
        return false;
    }
    list->groups = groups;
    return true;
}

static bool expected_group_list(
    const struct nb_session_credentials *credentials,
    struct nb_session_group_list *list)
{
    int capacity = 16;

    memset(list, 0, sizeof(*list));
    if (!resize_group_list(list, capacity)) {
        return false;
    }
    for (;;) {
        int count = capacity;
        const int result = getgrouplist(credentials->user,
                                        credentials->gid,
                                        list->groups,
                                        &count);

        if (result >= 0) {
            if (count <= 0 || count > capacity) {
                errno = EIO;
                return false;
            }
            list->count = count;
            return true;
        }
        if (count <= capacity || count > NB_SESSION_GROUP_COUNT_MAXIMUM) {
            errno = count > NB_SESSION_GROUP_COUNT_MAXIMUM
                        ? EOVERFLOW
                        : EIO;
            return false;
        }
        if (!resize_group_list(list, count)) {
            return false;
        }
        capacity = count;
    }
}

static int compare_gid(const void *left, const void *right)
{
    const gid_t left_gid = *(const gid_t *)left;
    const gid_t right_gid = *(const gid_t *)right;

    return left_gid < right_gid ? -1 : left_gid != right_gid ? 1 : 0;
}

static bool verify_initialized_groups(struct nb_session_group_list *expected)
{
    struct nb_session_group_list actual;
    int count;
    bool matches;

    memset(&actual, 0, sizeof(actual));
    count = getgroups(0, NULL);
    if (count <= 0 || !resize_group_list(&actual, count)) {
        free(actual.groups);
        return false;
    }
    actual.count = getgroups(count, actual.groups);
    if (actual.count != count) {
        free(actual.groups);
        errno = EIO;
        return false;
    }
    qsort(expected->groups,
          (size_t)expected->count,
          sizeof(*expected->groups),
          compare_gid);
    qsort(actual.groups,
          (size_t)actual.count,
          sizeof(*actual.groups),
          compare_gid);
    matches = expected->count == actual.count &&
              memcmp(expected->groups,
                     actual.groups,
                     (size_t)expected->count * sizeof(*expected->groups)) == 0;
    free(actual.groups);
    if (!matches) {
        errno = EPERM;
    }
    return matches;
}

static bool append_environment(char *destination,
                               size_t destination_size,
                               const char *name,
                               const char *value)
{
    const int length = snprintf(destination,
                                destination_size,
                                "%s=%s",
                                name,
                                value);

    return length >= 0 && (size_t)length < destination_size;
}

_Noreturn void nb_session_credentials_drop_and_exec(
    const struct nb_session_credentials *credentials,
    int ipc_fd,
    const char *core_path,
    char *const core_argv[])
{
    const char *gtk_menu_bridge = getenv("NIXBENCH_GTK_MENU_BRIDGE");
    const bool enable_gtk_menu_bridge =
        gtk_menu_bridge != NULL && strcmp(gtk_menu_bridge, "1") == 0;
    const char *xwayland_rootless = getenv("NIXBENCH_XWAYLAND_ROOTLESS");
    const bool enable_xwayland_rootless =
        xwayland_rootless != NULL && strcmp(xwayland_rootless, "1") == 0;
    const char *xwayland = getenv("NIXBENCH_XWAYLAND");
    const bool preserve_xwayland =
        enable_xwayland_rootless && xwayland != NULL && xwayland[0] == '/';
    const char *legacy_xwayland_association =
        getenv("NIXBENCH_XWAYLAND_LEGACY_ASSOCIATION");
    const bool enable_legacy_xwayland_association =
        legacy_xwayland_association != NULL &&
        strcmp(legacy_xwayland_association, "1") == 0;
    const char *trace_wayland = getenv("NIXBENCH_TRACE_WAYLAND");
    const bool enable_wayland_trace =
        trace_wayland != NULL && strcmp(trace_wayland, "1") == 0;
    char home_environment[NB_SESSION_CREDENTIALS_PATH_CAPACITY + 6];
    char shell_environment[NB_SESSION_CREDENTIALS_PATH_CAPACITY + 7];
    char user_environment[NB_SESSION_CREDENTIALS_USER_CAPACITY + 6];
    char logname_environment[NB_SESSION_CREDENTIALS_USER_CAPACITY + 9];
    char path_environment[sizeof(safe_path) + 5];
    char ipc_environment[32];
    char gtk_menu_bridge_environment[] = "NIXBENCH_GTK_MENU_BRIDGE=1";
    char xwayland_rootless_environment[] =
        "NIXBENCH_XWAYLAND_ROOTLESS=1";
    char xwayland_environment[NB_SESSION_CREDENTIALS_PATH_CAPACITY + 20];
    char legacy_xwayland_association_environment[] =
        "NIXBENCH_XWAYLAND_LEGACY_ASSOCIATION=1";
    char trace_wayland_environment[] = "NIXBENCH_TRACE_WAYLAND=1";
    struct nb_session_group_list expected_groups;
    size_t environment_count = 0;
    int ipc_environment_length;
    char *environment[12];

    if (!credential_record_is_valid(credentials) || core_path == NULL ||
        core_path[0] != '/' || core_argv == NULL || core_argv[0] == NULL ||
        core_argv[0][0] == '\0') {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "invalid credential-drop arguments",
                      EINVAL);
    }
    if (geteuid() != (uid_t)0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "credential transition requires effective uid 0",
                      EPERM);
    }
    if (!place_ipc_descriptor(ipc_fd)) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not place the session IPC descriptor at fd 3",
                      errno);
    }
    if (!ensure_standard_descriptors()) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not establish standard descriptors",
                      errno);
    }
    if (!reset_signals()) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not reset inherited signal state",
                      errno);
    }
    if (setsid() < 0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not create the desktop core session",
                      errno);
    }
    (void)umask((mode_t)077);
    if (!establish_safe_limits()) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not establish safe resource limits",
                      errno);
    }

#if defined(__linux__)
    if (prctl(PR_SET_KEEPCAPS, 0L, 0L, 0L, 0L) != 0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not disable retained Linux capabilities",
                      errno);
    }
#endif
    if (!expected_group_list(credentials, &expected_groups)) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not resolve the desktop user's groups",
                      errno);
    }
    if (initgroups(credentials->user, credentials->gid) != 0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not initialize supplementary groups",
                      errno);
    }
    if (!verify_initialized_groups(&expected_groups)) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "supplementary groups do not match the user account",
                      errno);
    }
    free(expected_groups.groups);
    if (setgid(credentials->gid) != 0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not set the desktop core gid",
                      errno);
    }
    if (setuid(credentials->uid) != 0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not set the desktop core uid",
                      errno);
    }
    if (!identities_match(credentials)) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "real, effective, or saved ids retained privilege",
                      EPERM);
    }
    errno = 0;
    if (seteuid((uid_t)0) == 0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "desktop core could regain uid 0",
                      EPERM);
    }
    if (errno != EPERM) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "unexpected result while checking uid 0 reacquisition",
                      errno != 0 ? errno : EIO);
    }
    if (geteuid() == (uid_t)0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "desktop core retained effective uid 0",
                      EPERM);
    }
#if defined(__linux__)
    if (prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) != 0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not disable future Linux privilege gains",
                      errno);
    }
#endif
    if (chdir(credentials->home) != 0) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not enter the desktop user's home directory",
                      errno);
    }

    close_unexpected_descriptors();
    if (!append_environment(home_environment,
                            sizeof(home_environment),
                            "HOME",
                            credentials->home) ||
        !append_environment(shell_environment,
                            sizeof(shell_environment),
                            "SHELL",
                            credentials->shell) ||
        !append_environment(user_environment,
                            sizeof(user_environment),
                            "USER",
                            credentials->user) ||
        !append_environment(logname_environment,
                            sizeof(logname_environment),
                            "LOGNAME",
                            credentials->user) ||
        !append_environment(path_environment,
                            sizeof(path_environment),
                            "PATH",
                            safe_path)) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not build the desktop core environment",
                      EOVERFLOW);
    }
    ipc_environment_length = snprintf(ipc_environment,
                                      sizeof(ipc_environment),
                                      "NIXBENCH_SESSION_FD=%d",
                                      NB_SESSION_CREDENTIALS_IPC_FD);
    if (ipc_environment_length < 0 ||
        (size_t)ipc_environment_length >= sizeof(ipc_environment)) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not build the session IPC environment",
                      EOVERFLOW);
    }
    environment[environment_count++] = home_environment;
    environment[environment_count++] = shell_environment;
    environment[environment_count++] = user_environment;
    environment[environment_count++] = logname_environment;
    environment[environment_count++] = path_environment;
    environment[environment_count++] = ipc_environment;
    if (enable_gtk_menu_bridge) {
        environment[environment_count++] = gtk_menu_bridge_environment;
    }
    if (enable_xwayland_rootless) {
        environment[environment_count++] = xwayland_rootless_environment;
    }
    if (preserve_xwayland) {
        if (!append_environment(xwayland_environment,
                                sizeof(xwayland_environment),
                                "NIXBENCH_XWAYLAND",
                                xwayland)) {
            child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                          "could not preserve the Xwayland override",
                          EOVERFLOW);
        }
        environment[environment_count++] = xwayland_environment;
    }
    if (enable_legacy_xwayland_association) {
        environment[environment_count++] =
            legacy_xwayland_association_environment;
    }
    if (enable_wayland_trace) {
        environment[environment_count++] = trace_wayland_environment;
    }
    environment[environment_count] = NULL;
    if (!set_descriptor_cloexec(NB_SESSION_CREDENTIALS_IPC_FD, false)) {
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not inherit the session IPC descriptor",
                      errno);
    }
    if (!unblock_signals_for_exec()) {
        (void)set_descriptor_cloexec(NB_SESSION_CREDENTIALS_IPC_FD, true);
        child_failure(NB_SESSION_CREDENTIALS_SETUP_EXIT,
                      "could not establish the core signal mask",
                      errno);
    }
    execve(core_path, core_argv, environment);
    {
        const int saved_error = errno;

        (void)set_descriptor_cloexec(NB_SESSION_CREDENTIALS_IPC_FD, true);
        child_failure(NB_SESSION_CREDENTIALS_EXEC_EXIT,
                      "could not execute the desktop core",
                      saved_error);
    }
}
