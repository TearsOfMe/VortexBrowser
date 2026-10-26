
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

int pcm_s16be_is_silent(const uint8_t *pcm, size_t byte_len, int threshold);

int main() {
    uint8_t silent[100] = {0};
    uint8_t loud[100] = {0};
    loud[0] = 0x10; loud[1] = 0x00;
    printf("Silent: %d, Loud: %d
", pcm_s16be_is_silent(silent, 100, 10), pcm_s16be_is_silent(loud, 100, 10));
    return 0;
}
