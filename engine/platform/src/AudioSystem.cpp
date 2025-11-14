#include "match/platform/AudioSystem.hpp"

#include <SDL2/SDL.h>

#include "match/app/AssetFS.hpp"

namespace match::platform {

AudioSystem::~AudioSystem() {
    Shutdown();
}

bool AudioSystem::Initialize() {
    if (initialized_) {
        return true;
    }
    const int mix_flags = MIX_INIT_OGG | MIX_INIT_MP3;
    int init_result = Mix_Init(mix_flags);
    if ((init_result & mix_flags) != mix_flags) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Mix_Init missing codecs: %s", Mix_GetError());
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        Mix_Quit();
        return false;
    }
    Mix_QuerySpec(&sample_rate_, &format_, &channels_);
    Mix_AllocateChannels(24);
    Mix_Volume(-1, static_cast<int>(MIX_MAX_VOLUME * 0.8f));
    Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * 0.6f));

    swap_ = LoadChunk("swap.wav");
    match_ = LoadChunk("match.wav");
    combo_ = LoadChunk("combo.wav");
    error_ = LoadChunk("error.wav");
    bomb_ = LoadChunk("bomb.wav");
    click_ = LoadChunk("click.wav");
    next_turn_ = LoadChunk("next_turn.wav");
    next_round_ = LoadChunk("next_round.wav");
    win_ = LoadChunk("win.wav");
    intro_ = LoadChunk("intro.wav");
    countdown_tick_ = LoadChunk("countdown.wav");
    countdown_final_ = LoadChunk("countdown_end.wav");
    music_ = LoadMusic({"bg_loop.ogg", "Vovkinshteyn.wav"});
    music_playing_ = false;

    initialized_ = true;
    return true;
}

void AudioSystem::Shutdown() {
    if (!initialized_) {
        return;
    }
    StopMusic();
    FreeChunk(swap_);
    FreeChunk(match_);
    FreeChunk(combo_);
    FreeChunk(error_);
    FreeChunk(bomb_);
    FreeChunk(click_);
    FreeChunk(next_turn_);
    FreeChunk(next_round_);
    FreeChunk(win_);
    FreeChunk(intro_);
    FreeChunk(countdown_tick_);
    FreeChunk(countdown_final_);
    if (music_) {
        Mix_FreeMusic(music_);
        music_ = nullptr;
    }
    Mix_CloseAudio();
    Mix_Quit();
    initialized_ = false;
}

void AudioSystem::PlaySwap() const {
    Play(swap_);
}

void AudioSystem::PlayMatch(bool cascade) const {
    if (cascade) {
        Play(combo_);
    } else {
        Play(match_);
    }
}

void AudioSystem::PlayBomb() const {
    Play(bomb_);
}

void AudioSystem::PlayError() const {
    Play(error_);
}

void AudioSystem::PlayClick() const {
    Play(click_);
}

float AudioSystem::PlayNextTurn() const {
    Play(next_turn_);
    return ChunkDurationMs(next_turn_);
}

float AudioSystem::PlayNextRound() const {
    Play(next_round_);
    return ChunkDurationMs(next_round_);
}

float AudioSystem::PlayWin() const {
    Play(win_);
    return ChunkDurationMs(win_);
}

float AudioSystem::PlayIntro() const {
    Play(intro_);
    return ChunkDurationMs(intro_);
}

void AudioSystem::PlayCountdownTick() const {
    Play(countdown_tick_);
}

void AudioSystem::PlayCountdownFinal() const {
    Play(countdown_final_);
}

void AudioSystem::StartMusicLoop() {
    if (!initialized_ || music_playing_ || !music_) {
        return;
    }
    if (Mix_PlayMusic(music_, -1) == 0) {
        music_playing_ = true;
    }
}

void AudioSystem::StopMusic() {
    if (!initialized_ || !music_playing_) {
        return;
    }
    Mix_HaltMusic();
    music_playing_ = false;
}

void AudioSystem::FreeChunk(Mix_Chunk*& chunk) {
    if (chunk) {
        Mix_FreeChunk(chunk);
        chunk = nullptr;
    }
}

void AudioSystem::Play(Mix_Chunk* chunk) {
    if (!chunk) {
        return;
    }
    Mix_PlayChannel(-1, chunk, 0);
}

Mix_Chunk* AudioSystem::LoadChunk(const std::string& filename) {
    std::filesystem::path path = match::app::AssetPath(filename);
    if (!match::app::FileExists(path)) {
        return nullptr;
    }
    return Mix_LoadWAV(path.string().c_str());
}

Mix_Music* AudioSystem::LoadMusic(std::initializer_list<const char*> candidates) {
    for (const char* name : candidates) {
        std::filesystem::path path = match::app::AssetPath(name);
        if (!match::app::FileExists(path)) {
            continue;
        }
        Mix_Music* music = Mix_LoadMUS(path.string().c_str());
        if (music) {
            return music;
        }
    }
    return nullptr;
}

float AudioSystem::ChunkDurationMs(Mix_Chunk* chunk) const {
    if (!chunk || sample_rate_ <= 0 || channels_ <= 0) {
        return 0.0f;
    }
    int bits = SDL_AUDIO_BITSIZE(format_);
    if (bits <= 0) {
        return 0.0f;
    }
    const int bytes_per_sample = (bits / 8) * channels_;
    if (bytes_per_sample <= 0) {
        return 0.0f;
    }
    return (chunk->alen * 1000.0f) / static_cast<float>(sample_rate_ * bytes_per_sample);
}

}  // namespace match::platform
