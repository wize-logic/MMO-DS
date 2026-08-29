/* The self-test driver. */
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <signal.h>
#endif

int codec_tests_run(void);
int net_tests_run(void);
int crypto_tests_run(void);
int checksum_tests_run(void);
int p256_tests_run(void);
int session_tests_run(void);
int login_tests_run(void);
int deflate_tests_run(void);
int handoff_tests_run(void);
int game_tests_run(void);
int client_tests_run(void);
int charcode_tests_run(void);
int osk_tests_run(void);
int entry_tests_run(void);
int save_policy_tests_run(void);
int entity_tests_run(void);
int idmap_tests_run(void);
int story_tests_run(void);
int script_tests_run(void);
int selftest_tests_run(void);
int mockserver_tests_run(void);
int trace_tests_run(void);
int capture_tests_run(void);
int view_channel_tests_run(void);
int view_geom_tests_run(void);
int text_channel_tests_run(void);
int status_channel_tests_run(void);
int hud_channel_tests_run(void);
int view_ui_tests_run(void);
int follower_tests_run(void);
int launcher_tests_run(void);
int logdir_tests_run(void);
int feed_tests_run(void);
int presence_tests_run(void);
int display_tests_run(void);
int sprite_tests_run(void);
int region_tests_run(void);
int cartridge_tests_run(void);
int appearance_tests_run(void);
int creator_tests_run(void);
int chatwin_tests_run(void);
int friendswin_tests_run(void);
int guildwin_tests_run(void);
int mapwin_tests_run(void);
int netwin_tests_run(void);
int partywin_tests_run(void);
int widget_tests_run(void);
int battle_anim_tests_run(void);

int main(void)
{
    int failures = 0;

#ifndef _WIN32
    /*
     * This driver plays both ends of several sockets, and a peer that closed first is a
     * legitimate outcome of a test rather than an error in one: the client half gives up on a
     * read and closes, and whichever suite is pretending to be the far end then writes into a
     * socket with nobody on it.
     */
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Line buffered even into a file. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    failures += codec_tests_run();
    failures += net_tests_run();
    failures += crypto_tests_run();
    failures += checksum_tests_run();
    failures += p256_tests_run();
    failures += session_tests_run();
    failures += login_tests_run();
    failures += deflate_tests_run();
    failures += handoff_tests_run();
    failures += game_tests_run();
    failures += client_tests_run();
    failures += charcode_tests_run();
    failures += osk_tests_run();
    failures += entry_tests_run();
    failures += save_policy_tests_run();
    failures += entity_tests_run();
    failures += idmap_tests_run();
    failures += display_tests_run();
    failures += sprite_tests_run();
    failures += region_tests_run();
    failures += cartridge_tests_run();
    failures += appearance_tests_run();
    failures += creator_tests_run();
    failures += chatwin_tests_run();
    failures += friendswin_tests_run();
    failures += guildwin_tests_run();
    failures += mapwin_tests_run();
    failures += netwin_tests_run();
    failures += partywin_tests_run();
    failures += widget_tests_run();
    failures += battle_anim_tests_run();
    failures += story_tests_run();
    failures += script_tests_run();
    failures += selftest_tests_run();
    failures += mockserver_tests_run();
    failures += trace_tests_run();
    failures += capture_tests_run();
    failures += view_channel_tests_run();
    failures += view_geom_tests_run();
    failures += text_channel_tests_run();
    failures += status_channel_tests_run();
    failures += hud_channel_tests_run();
    failures += view_ui_tests_run();
    failures += follower_tests_run();
    failures += launcher_tests_run();
    failures += logdir_tests_run();
    failures += feed_tests_run();
    failures += presence_tests_run();

    if (failures) {
        printf("suite: %d check(s) FAILED\n", failures);
        return EXIT_FAILURE;
    }
    printf("suite: all checks passed\n");
    return EXIT_SUCCESS;
}
