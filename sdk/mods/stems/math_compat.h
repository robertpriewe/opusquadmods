/* Use the original AArch64 libm ABI, also present in the older test runtime. */
#if defined(__linux__) && defined(__aarch64__)
__asm__(".symver pow,pow@GLIBC_2.17");
__asm__(".symver log,log@GLIBC_2.17");
__asm__(".symver exp,exp@GLIBC_2.17");
#endif
