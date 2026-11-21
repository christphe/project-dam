/* Step 1: the core links into a test binary and a harmless core function runs. */
#include "ge_test.h"
#include "defaults.h"

int main(void)
{
    double v = -1;
    GE_TEST_BEGIN("core link");
    GE_CHECK(defaultsGetFor(0, "Game.CheckUpdates", &v));
    GE_CHECK_EQ_INT(v, 0);
    return GE_TEST_END();
}
