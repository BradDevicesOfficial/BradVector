#include "brad/braddrive.h"
#include "brad/fabric.h"
#include "brad/spmp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(brad_drive_init() == 0);

    struct brad_drive_info info;
    assert(brad_drive_info(&info) == 0);
    assert(info.tier == BRAD_DRIVE_STANDARD);
    assert(info.capacity > 0);
    assert(info.seq_read_mbs == 14000);
    assert(info.seq_write_mbs == 10000);

    char buf[4096];
    int ret = brad_drive_read(0, buf, 8);
    assert(ret == 4096);

    const char *wb = "BradDrive write test";
    ret = brad_drive_write(100, wb, 1);
    assert(ret > 0);

    assert(brad_drive_trim(200, 16) == 0);
    assert(brad_drive_flush() == 0);

    uint64_t bytes_r = 0, bytes_w = 0;
    unsigned rd = 0, wr = 0, tr = 0, fl = 0;
    assert(brad_drive_query_stats(&bytes_r, &bytes_w, &rd, &wr, &tr, &fl) == 0);
    assert(bytes_r == 4096);
    assert(rd == 1);
    assert(wr == 1);
    assert(tr == 1);
    assert(fl == 1);

    uint64_t predicted[8];
    unsigned np = 8;
    assert(brad_drive_neuro_predict(0, 100, predicted, &np) == 0);
    assert(np > 0);

    printf("BradDrive tests passed\n");
    return 0;
}
