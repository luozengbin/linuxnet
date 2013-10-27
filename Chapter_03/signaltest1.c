#include <signal.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

/* sig_atomic_tはシグナルハンドラで安全に扱うためです */
volatile sig_atomic_t g_gotsig = 0;

/* SIGINTハンドラ
   kill -2 ${processid}で確認できる
 */
void
sig_int_handler(int sig)
{
    g_gotsig = sig;
}

int
main(int argc, char *argv[])
{
    struct sigaction sa;
    (void) sigaction(SIGINT, (struct sigaction *) NULL, &sa);
    sa.sa_handler = sig_int_handler;
    sa.sa_flags = SA_NODEFER;
    (void) sigaction(SIGINT, &sa, (struct sigaction *) NULL);
    while (g_gotsig == 0) {
        (void) fprintf(stderr, ".");
        (void) sleep(1);
    }
    (void) fprintf(stderr, "\nEND\n");
    return (EX_OK);
}
