/* wl82 overrides for the TuyaOpen mbedTLS build. */

/* mbedTLS's stock timing.c only supports POSIX and Windows hosts. */
#undef MBEDTLS_TIMING_C

/* Certificate bundles are supplied from flash/memory on the device. */
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
