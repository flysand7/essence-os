#include <essence.h>

long OSMakeLinuxSystemCall(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
	return EsPOSIXSystemCall(n, a1, a2, a3, a4, a5, a6);
}
