#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "backend_probe_drm_internal.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

struct fake_drm {
    char calls[32];
    size_t call_count;
    int read_write_open_result;
    int read_only_open_result;
    int master_result;
    bool is_master;
    int drop_result;
    int close_result;
};

static void record_call(struct fake_drm *fake, char call)
{
    if (fake->call_count + 1 < sizeof(fake->calls)) {
        fake->calls[fake->call_count++] = call;
        fake->calls[fake->call_count] = '\0';
    }
}

static int fake_open(void *opaque, const char *path, bool read_write)
{
    struct fake_drm *fake = opaque;

    CHECK(strcmp(path, "/dev/dri/card7") == 0);
    record_call(fake, read_write ? 'W' : 'R');
    return read_write ? fake->read_write_open_result
                      : fake->read_only_open_result;
}

static int fake_close(void *opaque, int descriptor)
{
    struct fake_drm *fake = opaque;

    CHECK(descriptor == 42);
    record_call(fake, 'X');
    return fake->close_result;
}

static int fake_is_master(void *opaque,
                          int descriptor,
                          bool *is_master)
{
    struct fake_drm *fake = opaque;

    CHECK(descriptor == 42);
    record_call(fake, 'M');
    if (fake->master_result == 0) {
        *is_master = fake->is_master;
    }
    return fake->master_result;
}

static int fake_drop_master(void *opaque, int descriptor)
{
    struct fake_drm *fake = opaque;

    CHECK(descriptor == 42);
    record_call(fake, 'D');
    return fake->drop_result;
}

static void fake_collect_version(
    void *opaque,
    int descriptor,
    struct nb_backend_probe_drm_card *card)
{
    struct fake_drm *fake = opaque;

    CHECK(descriptor == 42);
    record_call(fake, 'V');
    card->version.available = true;
    card->version.major = 1;
    (void)snprintf(card->version.name,
                   sizeof(card->version.name),
                   "%s",
                   "fake");
}

static void fake_collect_capabilities(
    void *opaque,
    int descriptor,
    struct nb_backend_probe_drm_card *card)
{
    struct fake_drm *fake = opaque;

    CHECK(descriptor == 42);
    record_call(fake, 'C');
    card->dumb_buffer.attempted = true;
    card->dumb_buffer.available = true;
    card->dumb_buffer.value = 1;
}

static void fake_collect_resources(
    void *opaque,
    int descriptor,
    struct nb_backend_probe_drm_card *card)
{
    struct fake_drm *fake = opaque;

    CHECK(descriptor == 42);
    record_call(fake, 'S');
    card->resources_available = true;
    card->crtc_count = 1;
    card->encoder_count = 1;
    card->reported_connector_count = 1;
    card->connector_count = 1;
    card->connectors[0].query_available = true;
    card->connectors[0].connection =
        NB_BACKEND_PROBE_DRM_CONNECTION_CONNECTED;
    card->connectors[0].mode_count = 1;
}

static const struct nb_backend_probe_drm_operations fake_operations = {
    fake_open,
    fake_close,
    fake_is_master,
    fake_drop_master,
    fake_collect_version,
    fake_collect_capabilities,
    fake_collect_resources
};

static void initialize_card(struct nb_backend_probe_drm_card *card)
{
    memset(card, 0, sizeof(*card));
    (void)snprintf(card->device.path,
                   sizeof(card->device.path),
                   "%s",
                   "/dev/dri/card7");
    card->device.exists = true;
    card->device.character_device = true;
}

static void initialize_fake(struct fake_drm *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->read_write_open_result = 42;
    fake->read_only_open_result = 42;
}

static void test_non_master_collection(void)
{
    struct nb_backend_probe_drm_card card;
    struct fake_drm fake;

    initialize_card(&card);
    initialize_fake(&fake);
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &fake_operations, &fake);

    CHECK(strcmp(fake.calls, "WMVCSX") == 0);
    CHECK(card.query_supported);
    CHECK(card.open_attempted);
    CHECK(card.open_mode == NB_BACKEND_PROBE_DRM_OPEN_READ_WRITE);
    CHECK(card.master_checked);
    CHECK(!card.implicit_master);
    CHECK(card.version.available);
    CHECK(card.resources_available);
    CHECK(card.close_error == 0);
    CHECK(nb_backend_probe_drm_card_is_kms_candidate(&card));
}

static void test_read_only_fallback(void)
{
    struct nb_backend_probe_drm_card card;
    struct fake_drm fake;

    initialize_card(&card);
    initialize_fake(&fake);
    fake.read_write_open_result = -EACCES;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &fake_operations, &fake);

    CHECK(strcmp(fake.calls, "WRMVCSX") == 0);
    CHECK(card.read_write_open_error == EACCES);
    CHECK(card.open_mode == NB_BACKEND_PROBE_DRM_OPEN_READ_ONLY);
    CHECK(!nb_backend_probe_drm_card_is_kms_candidate(&card));
}

static void test_open_failure(void)
{
    struct nb_backend_probe_drm_card card;
    struct fake_drm fake;

    initialize_card(&card);
    initialize_fake(&fake);
    fake.read_write_open_result = -EACCES;
    fake.read_only_open_result = -ENODEV;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &fake_operations, &fake);

    CHECK(strcmp(fake.calls, "WR") == 0);
    CHECK(card.open_mode == NB_BACKEND_PROBE_DRM_OPEN_NONE);
    CHECK(card.read_write_open_error == EACCES);
    CHECK(card.read_only_open_error == ENODEV);
    CHECK(!card.master_checked);
    CHECK(!card.version.available);
}

static void test_implicit_master_drop(void)
{
    struct nb_backend_probe_drm_card card;
    struct fake_drm fake;

    initialize_card(&card);
    initialize_fake(&fake);
    fake.is_master = true;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &fake_operations, &fake);

    CHECK(strcmp(fake.calls, "WMDVCSX") == 0);
    CHECK(card.implicit_master);
    CHECK(card.master_drop_attempted);
    CHECK(card.master_dropped);
    CHECK(card.master_drop_error == 0);
    CHECK(card.version.available);
    CHECK(nb_backend_probe_drm_card_is_kms_candidate(&card));
}

static void test_implicit_master_drop_failure(void)
{
    struct nb_backend_probe_drm_card card;
    struct fake_drm fake;

    initialize_card(&card);
    initialize_fake(&fake);
    fake.is_master = true;
    fake.drop_result = -EPERM;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &fake_operations, &fake);

    CHECK(strcmp(fake.calls, "WMDX") == 0);
    CHECK(card.master_drop_attempted);
    CHECK(!card.master_dropped);
    CHECK(card.master_drop_error == EPERM);
    CHECK(!card.version.available);
    CHECK(!card.resources_available);
    CHECK(!nb_backend_probe_drm_card_is_kms_candidate(&card));
}

static void test_master_check_and_close_failures(void)
{
    struct nb_backend_probe_drm_card card;
    struct fake_drm fake;

    initialize_card(&card);
    initialize_fake(&fake);
    fake.master_result = -EIO;
    fake.close_result = -EBADF;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &fake_operations, &fake);

    CHECK(strcmp(fake.calls, "WMX") == 0);
    CHECK(!card.master_checked);
    CHECK(card.master_check_error == EIO);
    CHECK(card.close_error == EBADF);
    CHECK(!card.version.available);
}

static void test_missing_safety_and_close_operations(void)
{
    struct nb_backend_probe_drm_card card;
    struct nb_backend_probe_drm_operations operations = fake_operations;
    struct fake_drm fake;

    initialize_card(&card);
    initialize_fake(&fake);
    operations.is_master = NULL;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &operations, &fake);
    CHECK(strcmp(fake.calls, "WX") == 0);
    CHECK(card.master_check_error == ENOSYS);
    CHECK(!nb_backend_probe_drm_card_is_kms_candidate(&card));

    initialize_card(&card);
    initialize_fake(&fake);
    operations = fake_operations;
    operations.drop_master = NULL;
    fake.is_master = true;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &operations, &fake);
    CHECK(strcmp(fake.calls, "WMX") == 0);
    CHECK(card.master_drop_attempted);
    CHECK(card.master_drop_error == ENOSYS);
    CHECK(!nb_backend_probe_drm_card_is_kms_candidate(&card));

    initialize_card(&card);
    initialize_fake(&fake);
    operations = fake_operations;
    operations.close_card = NULL;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &operations, &fake);
    CHECK(strcmp(fake.calls, "WMVCS") == 0);
    CHECK(card.close_error == ENOSYS);
    CHECK(!nb_backend_probe_drm_card_is_kms_candidate(&card));
}

static void test_missing_operations(void)
{
    struct nb_backend_probe_drm_card card;
    struct nb_backend_probe_drm_operations operations = fake_operations;
    struct fake_drm fake;

    initialize_card(&card);
    initialize_fake(&fake);
    operations.collect_version = NULL;
    operations.collect_capabilities = NULL;
    operations.collect_resources = NULL;
    nb_backend_probe_drm_collect_card_with_operations(
        &card, &operations, &fake);

    CHECK(strcmp(fake.calls, "WMX") == 0);
    CHECK(card.version.error == ENOSYS);
    CHECK(card.dumb_buffer.attempted);
    CHECK(card.dumb_buffer.error == ENOSYS);
    CHECK(card.resources_error == ENOSYS);
    CHECK(card.planes_error == ENOSYS);

    nb_backend_probe_drm_collect_card_with_operations(NULL,
                                                      &operations,
                                                      &fake);
    initialize_card(&card);
    nb_backend_probe_drm_collect_card_with_operations(&card, NULL, NULL);
    CHECK(!card.query_supported);
    CHECK(strcmp(card.device.path, "/dev/dri/card7") == 0);
}

int main(void)
{
    test_non_master_collection();
    test_read_only_fallback();
    test_open_failure();
    test_implicit_master_drop();
    test_implicit_master_drop_failure();
    test_master_check_and_close_failures();
    test_missing_safety_and_close_operations();
    test_missing_operations();

    if (failures != 0) {
        fprintf(stderr, "DRM probe tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("DRM probe tests: ok");
    return 0;
}
