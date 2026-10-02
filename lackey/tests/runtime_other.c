/* A separate compilation unit makes file filters observable. */
__attribute__((noinline))
void lackey_test_beta(volatile unsigned *destination)
{
   *destination = 17;
}
