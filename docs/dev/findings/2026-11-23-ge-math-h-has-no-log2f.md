# GE's math.h has no log2f: a call compiles as an int function

**Trap.** Port code that includes `<math.h>` next to the game headers gets GE's own `math.h`, which declares `sqrtf` and a few others but not `log2f`. A call to `log2f` is then an implicit declaration: GCC only warns (`implicit declaration of function 'log2f'`, `incompatible implicit declaration of built-in function`), treats the result as an `int`, and the value is garbage.

**Symptom.** The explosion lights' brightness-by-size factor (`port/src/dynlights.c`) came out wrong in game while the code read right; the build log carried the two warnings above. `relight.c` had the same warnings for `fabsf` and `qsort`.

**What works.** Use the compiler builtin (`__builtin_log2f`), or declare the prototype yourself. Treat any `implicit declaration` warning in a port file as an error: the function exists at link time, so nothing else catches it.
