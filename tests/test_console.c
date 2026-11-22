/* CONSOLE mode precedence (port/include/console.h): pure, header only. */
#include "ge_test.h"
#include "../port/include/console.h"

int main(void)
{
    int why;
    GE_TEST_BEGIN("console");
    /* Switch: always on, whatever the env/ini say */
    GE_CHECK_EQ_INT(portConsoleResolve(1, "0", 0, &why), 1);
    GE_CHECK_EQ_INT(why, PORT_CONSOLE_SWITCH);
    GE_CHECK_EQ_INT(portConsoleResolve(1, NULL, -1, NULL), 1);
    /* elsewhere: default off */
    GE_CHECK_EQ_INT(portConsoleResolve(0, NULL, -1, &why), 0);
    GE_CHECK_EQ_INT(why, PORT_CONSOLE_OFF);
    /* env wins over the ini, both ways */
    GE_CHECK_EQ_INT(portConsoleResolve(0, "1", 0, &why), 1);
    GE_CHECK_EQ_INT(why, PORT_CONSOLE_ENV);
    GE_CHECK_EQ_INT(portConsoleResolve(0, "0", 1, &why), 0);
    GE_CHECK_EQ_INT(why, PORT_CONSOLE_ENV);
    /* ini when no (valid) env */
    GE_CHECK_EQ_INT(portConsoleResolve(0, NULL, 1, &why), 1);
    GE_CHECK_EQ_INT(why, PORT_CONSOLE_INI);
    GE_CHECK_EQ_INT(portConsoleResolve(0, "", 1, &why), 1);
    GE_CHECK_EQ_INT(portConsoleResolve(0, "yes", 0, &why), 0);
    GE_CHECK_EQ_INT(why, PORT_CONSOLE_INI);
    GE_CHECK_EQ_INT(portConsoleResolve(0, NULL, 7, &why), 0);
    GE_CHECK_EQ_INT(why, PORT_CONSOLE_OFF);
    return GE_TEST_END();
}
