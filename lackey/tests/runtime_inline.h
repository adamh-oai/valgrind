static __attribute__((always_inline)) inline
void lackey_inline_target(volatile unsigned *destination)
{
   *destination = 29;
}
