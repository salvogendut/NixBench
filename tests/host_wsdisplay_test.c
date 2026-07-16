#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include "host_wsdisplay.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void check_creation_failure(int expected_system_error)
{
    CHECK(nb_host_wsdisplay_creation_error() != NULL);
    CHECK(nb_host_wsdisplay_creation_error()[0] != '\0');
    CHECK(nb_host_wsdisplay_creation_system_error() ==
          expected_system_error);
}

int main(void)
{
    struct nb_host_wsdisplay_options options;

    errno = 0;
    CHECK(nb_host_wsdisplay_request_vt_switch(NULL, 1) ==
          NB_HOST_RESULT_INVALID_ARGUMENT);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(nb_host_wsdisplay_request_vt_switch(NULL, 0) ==
          NB_HOST_RESULT_INVALID_ARGUMENT);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(nb_host_wsdisplay_request_vt_switch(NULL, 13) ==
          NB_HOST_RESULT_INVALID_ARGUMENT);
    CHECK(errno == EINVAL);

    nb_host_wsdisplay_options_init(NULL);
    CHECK(nb_host_wsdisplay_create(NULL) == NULL);
    check_creation_failure(EINVAL);

    nb_host_wsdisplay_options_init(&options);
    CHECK(options.device_path != NULL);
    CHECK(options.device_path[0] != '\0');
    CHECK(options.expected_active_vt == 0);

    options.device_path = "";
    CHECK(nb_host_wsdisplay_create(&options) == NULL);
    check_creation_failure(EINVAL);

    nb_host_wsdisplay_options_init(&options);
    options.expected_active_vt = -1;
    CHECK(nb_host_wsdisplay_create(&options) == NULL);
    check_creation_failure(EINVAL);

#if !defined(__NetBSD__)
    nb_host_wsdisplay_options_init(&options);
    CHECK(nb_host_wsdisplay_create(&options) == NULL);
    CHECK(nb_host_wsdisplay_creation_error()[0] != '\0');
    CHECK(nb_host_wsdisplay_creation_system_error() != 0);
#endif

    if (failures != 0) {
        fprintf(stderr,
                "wsdisplay host tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("wsdisplay host tests: ok");
    return 0;
}
