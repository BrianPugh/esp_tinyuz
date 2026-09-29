#include <stdlib.h>

#include "sdkconfig.h"
#include "unity.h"

void app_main(void)
{
#if CONFIG_IDF_TARGET_LINUX
    /* Host run: execute every test and report through the exit code. */
    UNITY_BEGIN();
    unity_run_all_tests();
    exit(UNITY_END());
#else
    unity_run_menu();
#endif
}
