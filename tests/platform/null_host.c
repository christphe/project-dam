/* What main.c provides to the core: the tests are the host program, so they supply it here. */

/* Defined in port/src/main.c (not part of the core), called by the options overlay: the tests have no
 * unlock flags to seed. */
void portAllUnlockedApply(void) {}
