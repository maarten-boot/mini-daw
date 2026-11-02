// Virtual MIDI keyboard for hot-plug tests (Linux, ALSA sequencer):
// a port "Test Keys" that sends a note every 500 ms for <seconds> (default 5),
// then exits, which looks like unplugging a keyboard.
// Build: gcc -O1 -o vkeys vkeys.c -lasound    Run: ./vkeys 10
// Part of mini-daw, MIT licence.
#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char** argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 5;
    snd_seq_t* seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) return 1;
    snd_seq_set_client_name(seq, "Test Keys");
    int port = snd_seq_create_simple_port(seq, "Test Keys MIDI 1",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_HARDWARE);
    for (int i = 0; i < secs * 2; ++i) {
        snd_seq_event_t ev; snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, port); snd_seq_ev_set_subs(&ev); snd_seq_ev_set_direct(&ev);
        snd_seq_ev_set_noteon(&ev, 0, 60 + (i % 12), 100);
        snd_seq_event_output_direct(seq, &ev);
        usleep(100000);
        snd_seq_ev_set_noteoff(&ev, 0, 60 + (i % 12), 0);
        snd_seq_event_output_direct(seq, &ev);
        usleep(400000);
    }
    snd_seq_close(seq);
    return 0;
}
