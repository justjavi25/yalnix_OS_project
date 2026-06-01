#include <hardware.h>
#include <yuser.h>

extern char _end;

int main(void)
{
    char *base = (char *)UP_TO_PAGE(&_end);
    char *one_page = base + PAGESIZE;
    char *two_pages = base + (2 * PAGESIZE);
    int rc;

    TracePrintf(0, "cp3_brk: base=%x one_page=%x two_pages=%x\n",
                base, one_page, two_pages);

    rc = Brk(one_page);
    TracePrintf(0, "cp3_brk: Brk(one_page) rc=%d\n", rc);
    if (rc == 0) {
        base[0] = 'A';
        one_page[-1] = 'Z';
        TracePrintf(0, "cp3_brk: wrote first grown page: %c %c\n",
                    base[0], one_page[-1]);
    }

    rc = Brk(two_pages);
    TracePrintf(0, "cp3_brk: Brk(two_pages) rc=%d\n", rc);
    if (rc == 0) {
        one_page[0] = 'B';
        two_pages[-1] = 'Y';
        TracePrintf(0, "cp3_brk: wrote second grown page: %c %c\n",
                    one_page[0], two_pages[-1]);
    }

    rc = Brk(one_page);
    TracePrintf(0, "cp3_brk: shrink back to one_page rc=%d\n", rc);
    TracePrintf(0, "cp3_brk: PASS if all Brk rc values above are 0 and writes printed expected letters\n");

    while (1) {
        Pause();
    }
}
