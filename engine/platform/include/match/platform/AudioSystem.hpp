#pragma once

#include <SDL2/SDL_mixer.h>

#include <initializer_list>
#include <string>

namespace match::platform {

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    bool Initialize();
    void Shutdown();

    void StartMusicLoop();
    void StopMusic();

    void PlaySwap() const;
    void PlayMatch(bool cascade) const;
    void PlayBomb() const;
    void PlayError() const;
    void PlayClick() const;
    float PlayNextTurn() const;
    float PlayNextRound() const;
    float PlayWin() const;
    float PlayIntro() const;
    void PlayCountdownTick() const;
    void PlayCountdownFinal() const;

private:
    static void FreeChunk(Mix_Chunk*& chunk);
    static void Play(Mix_Chunk* chunk);
    float ChunkDurationMs(Mix_Chunk* chunk) const;

    Mix_Chunk* LoadChunk(const std::string& filename);
    Mix_Music* LoadMusic(std::initializer_list<const char*> candidates);

    bool initialized_ = false;
    Mix_Chunk* swap_ = nullptr;
    Mix_Chunk* match_ = nullptr;
    Mix_Chunk* combo_ = nullptr;
    Mix_Chunk* error_ = nullptr;
    Mix_Chunk* bomb_ = nullptr;
    Mix_Chunk* click_ = nullptr;
    Mix_Chunk* next_turn_ = nullptr;
    Mix_Chunk* next_round_ = nullptr;
    Mix_Chunk* win_ = nullptr;
    Mix_Chunk* intro_ = nullptr;
    Mix_Chunk* countdown_tick_ = nullptr;
    Mix_Chunk* countdown_final_ = nullptr;
    Mix_Music* music_ = nullptr;
    bool music_playing_ = false;
    int sample_rate_ = 44100;
    int channels_ = 2;
    Uint16 format_ = MIX_DEFAULT_FORMAT;
};

}  // namespace match::platform
