#include "session_credentials.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct fake_lookup {
    struct nb_session_account by_name;
    struct nb_session_account by_uid;
    int name_result;
    int uid_result;
    unsigned int name_calls;
    unsigned int uid_calls;
};

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr,                                                   \
                    "%s:%d: check failed: %s\n",                             \
                    __FILE__,                                                 \
                    __LINE__,                                                 \
                    #expression);                                             \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static int fake_by_name(void *opaque,
                        const char *name,
                        struct nb_session_account *account)
{
    struct fake_lookup *fake = opaque;

    ++fake->name_calls;
    if (fake->name_result != 0) {
        return fake->name_result;
    }
    CHECK(strcmp(name, "alice") == 0);
    *account = fake->by_name;
    return 0;
}

static int fake_by_uid(void *opaque,
                       uid_t uid,
                       struct nb_session_account *account)
{
    struct fake_lookup *fake = opaque;

    ++fake->uid_calls;
    if (fake->uid_result != 0) {
        return fake->uid_result;
    }
    CHECK(uid == (uid_t)1001);
    *account = fake->by_uid;
    return 0;
}

static struct fake_lookup valid_lookup(void)
{
    struct fake_lookup fake;
    const struct nb_session_account account = {
        .uid = (uid_t)1001,
        .gid = (gid_t)1002,
        .name = "alice",
        .home = "/home/alice",
        .shell = "/bin/sh"
    };

    memset(&fake, 0, sizeof(fake));
    fake.by_name = account;
    fake.by_uid = account;
    return fake;
}

static bool resolve(const struct nb_session_sudo_identity *identity,
                    struct fake_lookup *fake,
                    struct nb_session_credentials *credentials,
                    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY])
{
    const struct nb_session_account_lookup lookup = {
        .by_name = fake_by_name,
        .by_uid = fake_by_uid
    };

    return nb_session_credentials_resolve_with_lookup(identity,
                                                       &lookup,
                                                       fake,
                                                       credentials,
                                                       error);
}

static void test_valid_identity(void)
{
    const struct nb_session_sudo_identity identity = {
        .uid_text = "1001",
        .gid_text = "1002",
        .user = "alice"
    };
    struct fake_lookup fake = valid_lookup();
    struct nb_session_credentials credentials;
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY];

    CHECK(resolve(&identity, &fake, &credentials, error));
    CHECK(error[0] == '\0');
    CHECK(credentials.uid == (uid_t)1001);
    CHECK(credentials.gid == (gid_t)1002);
    CHECK(strcmp(credentials.user, "alice") == 0);
    CHECK(strcmp(credentials.home, "/home/alice") == 0);
    CHECK(strcmp(credentials.shell, "/bin/sh") == 0);
    CHECK(fake.name_calls == 1U);
    CHECK(fake.uid_calls == 1U);
}

static void expect_early_rejection(
    const struct nb_session_sudo_identity *identity)
{
    struct fake_lookup fake = valid_lookup();
    struct nb_session_credentials credentials;
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY];

    memset(&credentials, 0xa5, sizeof(credentials));
    CHECK(!resolve(identity, &fake, &credentials, error));
    CHECK(error[0] != '\0');
    CHECK(credentials.uid == (uid_t)0);
    CHECK(credentials.user[0] == '\0');
    CHECK(fake.name_calls == 0U);
    CHECK(fake.uid_calls == 0U);
}

static void test_environment_validation(void)
{
    const struct nb_session_sudo_identity missing_uid = {
        .uid_text = NULL, .gid_text = "1002", .user = "alice"
    };
    const struct nb_session_sudo_identity missing_gid = {
        .uid_text = "1001", .gid_text = "", .user = "alice"
    };
    const struct nb_session_sudo_identity missing_user = {
        .uid_text = "1001", .gid_text = "1002", .user = NULL
    };
    const struct nb_session_sudo_identity signed_uid = {
        .uid_text = "+1001", .gid_text = "1002", .user = "alice"
    };
    const struct nb_session_sudo_identity spaced_gid = {
        .uid_text = "1001", .gid_text = "1002 ", .user = "alice"
    };
    const struct nb_session_sudo_identity root = {
        .uid_text = "0", .gid_text = "0", .user = "root"
    };
    const struct nb_session_sudo_identity enormous = {
        .uid_text = "9999999999999999999999999999999999999999",
        .gid_text = "1002",
        .user = "alice"
    };

    expect_early_rejection(&missing_uid);
    expect_early_rejection(&missing_gid);
    expect_early_rejection(&missing_user);
    expect_early_rejection(&signed_uid);
    expect_early_rejection(&spaced_gid);
    expect_early_rejection(&root);
    expect_early_rejection(&enormous);
}

static void test_name_lookup_failures(void)
{
    const struct nb_session_sudo_identity identity = {
        .uid_text = "1001", .gid_text = "1002", .user = "alice"
    };
    struct fake_lookup fake;
    struct nb_session_credentials credentials;
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY];

    fake = valid_lookup();
    fake.name_result = ENOENT;
    CHECK(!resolve(&identity, &fake, &credentials, error));
    CHECK(fake.name_calls == 1U && fake.uid_calls == 0U);

    fake = valid_lookup();
    fake.by_name.uid = (uid_t)1003;
    CHECK(!resolve(&identity, &fake, &credentials, error));

    fake = valid_lookup();
    fake.by_name.gid = (gid_t)1003;
    CHECK(!resolve(&identity, &fake, &credentials, error));

    fake = valid_lookup();
    fake.by_name.name = "mallory";
    CHECK(!resolve(&identity, &fake, &credentials, error));

    fake = valid_lookup();
    fake.by_name.home = "relative/home";
    CHECK(!resolve(&identity, &fake, &credentials, error));

    fake = valid_lookup();
    fake.by_name.shell = "bin/sh";
    CHECK(!resolve(&identity, &fake, &credentials, error));
}

static void test_uid_lookup_mismatches(void)
{
    const struct nb_session_sudo_identity identity = {
        .uid_text = "1001", .gid_text = "1002", .user = "alice"
    };
    struct fake_lookup fake;
    struct nb_session_credentials credentials;
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY];

    fake = valid_lookup();
    fake.uid_result = EIO;
    CHECK(!resolve(&identity, &fake, &credentials, error));
    CHECK(fake.name_calls == 1U && fake.uid_calls == 1U);

    fake = valid_lookup();
    fake.by_uid.uid = (uid_t)1003;
    CHECK(!resolve(&identity, &fake, &credentials, error));

    fake = valid_lookup();
    fake.by_uid.gid = (gid_t)1003;
    CHECK(!resolve(&identity, &fake, &credentials, error));

    fake = valid_lookup();
    fake.by_uid.name = "mallory";
    CHECK(!resolve(&identity, &fake, &credentials, error));

    fake = valid_lookup();
    fake.by_uid.home = "/different";
    CHECK(!resolve(&identity, &fake, &credentials, error));

    fake = valid_lookup();
    fake.by_uid.shell = "/bin/ksh";
    CHECK(!resolve(&identity, &fake, &credentials, error));
}

static void test_invalid_api_arguments(void)
{
    const struct nb_session_sudo_identity identity = {
        .uid_text = "1001", .gid_text = "1002", .user = "alice"
    };
    struct fake_lookup fake = valid_lookup();
    const struct nb_session_account_lookup lookup = {
        .by_name = fake_by_name,
        .by_uid = fake_by_uid
    };
    struct nb_session_credentials credentials;
    char error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY];

    CHECK(!nb_session_credentials_resolve_with_lookup(NULL,
                                                       &lookup,
                                                       &fake,
                                                       &credentials,
                                                       error));
    CHECK(!nb_session_credentials_resolve_with_lookup(&identity,
                                                       NULL,
                                                       &fake,
                                                       &credentials,
                                                       error));
    CHECK(!nb_session_credentials_resolve_with_lookup(&identity,
                                                       &lookup,
                                                       &fake,
                                                       NULL,
                                                       error));
}

int main(void)
{
    test_valid_identity();
    test_environment_validation();
    test_name_lookup_failures();
    test_uid_lookup_mismatches();
    test_invalid_api_arguments();

    if (failures != 0) {
        fprintf(stderr,
                "session credential tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("session credential tests: ok");
    return 0;
}
