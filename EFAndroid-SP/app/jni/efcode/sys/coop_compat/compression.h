/* Stand-in for Apple's <compression.h>: compression_encode_buffer / compression_decode_buffer
   with COMPRESSION_ZLIB are defined in sys_coop_compat.h (zlib raw deflate), which is
   force-included when sys_coop_net.c is compiled on Windows and Linux. */
