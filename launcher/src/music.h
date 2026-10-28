// Athelas - the front screen's music: each game's own menu theme, read from the player's copy of the game when its
// box comes to the front, decoded once on a worker thread, looped, with a crossfade when the box changes. Nothing is
// copied or saved.
//   The Two Towers (Xbox): its main menu's theme (INTBASE1) in SOUND\S_WB_INTRFACE.xwb, a wave bank of Xbox ADPCM
//   The Fellowship of the Ring (Xbox): the title screen's music, the sound track of Movies\start_menu.xmv (PCM)
//   The Return of the King: not yet (its front-end music is packed in its own format)
#pragma once
#include <string>

enum MusicTrack { MUSIC_NONE, MUSIC_TT, MUSIC_FOTR };

bool music_init();                                    // opens the sound device (false: no sound, nothing plays)
// what to play now: the track and the game it is read from (a folder or a disc image); the same again does nothing
void music_play(MusicTrack t, const std::string &game);
void music_enable(bool on);                           // the Music button: fades out / back in
void music_pause(bool paused);                        // a game is playing: silent until it closes
void music_shutdown();
