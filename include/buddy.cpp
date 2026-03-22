//
// Created by lenny on 21.03.2026.
//

#include "buddy.hpp"

#include <utility>
#include <cmath>

namespace ByteBuddy {

    // -------------------------------------------------------------------------
    // Free helpers
    // -------------------------------------------------------------------------

    std::string getCatStateName(BuddyState state) {
        switch (state) {
            case BuddyState::Idle_0:  return "idle_0";
            case BuddyState::Idle_1:  return "idle_1";
            case BuddyState::Clean_0: return "clean_0";
            case BuddyState::Clean_1: return "clean_1";
            case BuddyState::Walk:    return "walk";
            case BuddyState::Run:     return "run";
            case BuddyState::Jump:    return "jump";
            case BuddyState::Sleep:   return "sleep";
        }
        return "idle_0";
    }

    BuddyState getCatStateFromName(const std::string& name) {
        if (name == "idle_0")  return BuddyState::Idle_0;
        if (name == "idle_1")  return BuddyState::Idle_1;
        if (name == "clean_0") return BuddyState::Clean_0;
        if (name == "clean_1") return BuddyState::Clean_1;
        if (name == "walk")    return BuddyState::Walk;
        if (name == "run")     return BuddyState::Run;
        if (name == "jump")    return BuddyState::Jump;
        if (name == "sleep")   return BuddyState::Sleep;
        return BuddyState::Idle_0;
    }

    BuddyState randomState() {
        static constexpr int count = static_cast<int>(BuddyState::Sleep) + 1;
        std::uniform_int_distribution<int> dist(0, count - 1);
        return static_cast<BuddyState>(dist(rng));
    }

    static float distanceTo(float x1, float y1, float x2, float y2) {
        return std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    }

    // -------------------------------------------------------------------------
    // Buddy
    // -------------------------------------------------------------------------

    std::string Buddy::getTextureName() const {
        if (b_state != BuddyState::Jump)
            return b_name + "_" + ByteBuddy::getCatStateName(b_state) + "_" + std::to_string(b_frame);

        switch (b_jumpPhase) {
            case JumpPhase::PreJump:   return b_name + "_jump_0";
            case JumpPhase::MidJump:   return b_name + "_jump_1";
            case JumpPhase::MidAir:    return b_name + "_jump_2";
            case JumpPhase::MidFall_0: return b_name + "_jump_3";
            case JumpPhase::MidFall_1: return b_name + "_jump_4";
            case JumpPhase::Landing_0: return b_name + "_jump_5";
            case JumpPhase::Landing_1: return b_name + "_jump_6";
        }
        return b_name + "_jump_0";
    }

    bool Buddy::hasAnimation(const std::string& ani_name) {
        static const std::vector<std::string> known = {
            "idle_0", "idle_1", "clean_0", "clean_1",
            "walk", "run", "jump", "hit", "sleep"
        };
        return std::ranges::find(known, ani_name) != known.end();
    }

    void Buddy::setAnimation(const std::string& ani_name) {
        b_state      = hasAnimation(ani_name) ? getCatStateFromName(ani_name) : BuddyState::Idle_0;
        b_frame      = 0;
        b_frameTimer = 0.f;
    }

    // -------------------------------------------------------------------------
    // Build a smooth parabolic arc from the current position to
    // (targetX, groundY). One deque entry is generated per step so the Buddy
    // moves one pixel-group each tick rather than jumping between 7 keyframes.
    // -------------------------------------------------------------------------
    void Buddy::updateTrajectory(float targetX, float targetY, float groundY) {
        b_trajectory.clear();

        const float startX = b_x;
        const float startY = b_y;
        const float dx     = abs(targetX - startX);
        const float dist   = std::abs(dx);

        // --- 50 % chance: jump toward targetY, otherwise use arc based on dist ---
        std::uniform_int_distribution<int> coinFlip(0, 1);
        const bool jumpToTarget = coinFlip(rng) == 1;
        const float jumpHeight  = jumpToTarget ? targetY : std::min(dist * 0.4f, static_cast<float>(b_windowBounds.height-20));

        auto arcY = [&](float t) -> float {
            const float baseline = startY + (groundY - startY) * t;
            const float arc      = jumpHeight * 4.f * t * (1.f - t);
            return baseline - arc;
        };

        struct PhaseRange { JumpPhase phase; float tStart; float tEnd; };
        static constexpr PhaseRange ranges[] = {
            { JumpPhase::PreJump,   0.00f, 0.01f },
            { JumpPhase::MidJump,   0.10f, 0.45f },
            { JumpPhase::MidAir,    0.30f, 0.55f },
            { JumpPhase::MidFall_0, 0.65f, 0.90f },
            { JumpPhase::MidFall_1, 0.75f, 0.98f },
            { JumpPhase::Landing_0, 0.90f, 0.99f },
            { JumpPhase::Landing_1, 0.97f, 1.00f },
        };

        // --- Consistent speed: fixed pixels-per-step regardless of distance ---
        static constexpr float PIXELS_PER_STEP = 6.f;
        const int totalSteps = std::max(1, static_cast<int>(dist / PIXELS_PER_STEP));

        for (int i = 0; i <= totalSteps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(totalSteps);

            JumpPhase phase = JumpPhase::Landing_1;
            for (const auto& r : ranges) {
                if (t >= r.tStart && t < r.tEnd) { phase = r.phase; break; }
            }

            b_trajectory.push_back({ { startX + dx * t, arcY(t) }, phase });
        }
    }

    void Buddy::updateDirection() {
        // Clean and Sleep animations look wrong when flipped mid-clip.
        if (b_state == BuddyState::Clean_0 ||
            b_state == BuddyState::Clean_1 ||
            b_state == BuddyState::Sleep)
            return;

        b_direction =  b_targetX > b_x ? 1 : -1;
    }

    void Buddy::updateBuddy(float mouseX, float mouseY) {

        if (b_state != BuddyState::Walk && b_state != BuddyState::Run) {
            b_targetX = mouseX;
        }

        // --- Frame animation --------------------------------------------------
        b_frameTimer += 1.f;
        if (b_frameTimer >= b_frameInterval) {
            b_frameTimer = 0.f;
            b_frame++;
            // Wrap: if the next frame texture is missing, restart the strip.
            if (!b_texHandler->exists(getTextureName()))
                b_frame = 0;
        }

        // --- State timer ------------------------------------------------------
        b_stateTimer += 1.f;
        if (b_stateTimer >= b_stateTimeMax) {
            b_stateTimer = 0.f;

            BuddyState newState = randomState();
            std::uniform_int_distribution<int> distTime(b_stateTimerMin, b_stateTimerMax);
            b_stateTimeMax = static_cast<float>(distTime(rng));

            setAnimation(getCatStateName(newState));

            if (b_state == BuddyState::Jump) {
                updateDirection();
                // Jump requires the mouse to be far enough away to make sense.
                if (distanceTo(mouseX, mouseY, b_x, b_y) < 50.f) {
                    b_stateTimer = b_stateTimeMax;
                } else {
                    std::uniform_int_distribution<int> jitter(-25, 25);

                    float targetX = mouseX + static_cast<float>(jitter(rng));
                    float targetY = 5 + mouseY + static_cast<float>(jitter(rng));

                    updateTrajectory(targetX, targetY, static_cast<float>(b_windowBounds.ground));
                }
            }

            if (b_state == BuddyState::Walk || b_state == BuddyState::Run) {
                if (std::abs(b_x - b_targetX) <= 25) {
                    b_stateTimer = b_stateTimeMax;
                } else {
                    std::uniform_int_distribution<int> isMouseTargetDist(0, 100);
                    if (isMouseTargetDist(rng) < 50) {
                        b_targetX = mouseX;
                    } else {
                        std::uniform_int_distribution<int> randomPositionDist(0, b_windowBounds.width);
                        b_targetX = static_cast<float>(randomPositionDist(rng));
                    }
                }
            }
        }

        // --- Per-state movement -----------------------------------------------
        if (b_state == BuddyState::Jump) {
            if (b_trajectory.empty()) {
                // Arc finished — return to idle with a fresh random duration.
                setAnimation("idle_0");
                b_stateTimer = 0.f;
                std::uniform_int_distribution<int> distTime(180, 600);
                b_stateTimeMax = static_cast<float>(distTime(rng));
            } else {
                const target& cur = b_trajectory.front();
                b_x         = cur.pos[0];
                b_y         = cur.pos[1];
                b_jumpPhase = cur.phase;
                b_trajectory.pop_front();
            }
        } else {
            updateDirection();

            if (b_state == BuddyState::Walk) {
                if (std::abs(b_x - b_targetX) <= 25) {
                    b_stateTimer = b_stateTimeMax;
                }
                b_x += 2.f * b_speed * static_cast<float>(b_direction);
            }

            if (b_state == BuddyState::Run) {
                if (std::abs(b_x - b_targetX) <= 25) {
                    b_stateTimer = b_stateTimeMax;
                }
                b_x += 4.f * b_speed * static_cast<float>(b_direction);
            }

            // Gravity: drift to ground, clamp so we never overshoot.
            if      (b_y < b_windowBounds.ground) {
                b_y += 2.f;
            }
            else if (b_y > b_windowBounds.ground) {
                b_y  = static_cast<float>(b_windowBounds.ground);
            };
        }

        // --- Proximity speech bubble ------------------------------------------
        if (abs(mouseX - b_x) < b_textureW && abs(mouseY - b_y) < b_textureH) {
            b_textRenderer->drawText("MEOW", 32, b_x, b_y - 48.f, 0, {255, 255, 255, 255}, 0, true);
        }
    }

    void Buddy::updateTextureSize() {
        SDL_Texture* tex = b_texHandler->getTexture(b_texHandler->getTextureLayer(getTextureName()));
        b_textureH = tex->h;
        b_textureW = tex->w;
    }

    void Buddy::renderBuddy() {
        updateTextureSize();

        // Left = no flip; Right = horizontal flip.
        SDL_FlipMode flipMode = (b_direction == -1)
            ? SDL_FlipMode::SDL_FLIP_HORIZONTAL
            : SDL_FlipMode::SDL_FLIP_NONE;

        if (!b_texHandler->renderTexture(
                getTextureName(),
                b_x - (0.5f * b_textureW), b_y - (0.5f * b_textureH),
                JFLX::SDL3::renderMode::JFLX_RENDER_BOTTOM_CENTERED,
                b_scale, b_scale, 0, flipMode, {255, 255, 255, 255}))
        {
            SDL_Log("Failed to render texture: %s", getTextureName().c_str());
        }
    }

    void Buddy::nextState() {
        b_stateTimer = b_stateTimeMax + 1;
    }

    // -------------------------------------------------------------------------
    // Setters / Getters
    // -------------------------------------------------------------------------

    void Buddy::setName(const std::string& name)                        { b_name = name; }
    void Buddy::setTextRenderer(JFLX::SDL3::TextRenderer* tr)           { b_textRenderer = tr; }
    void Buddy::setTextureHandler(JFLX::SDL3::TextureHandler* th)       { b_texHandler = th; }
    void Buddy::setWindowBounds(const windowBounds& bounds)             { b_windowBounds = bounds; }
    void Buddy::setState(BuddyState state, float mouseX, float mouseY)  {
        setAnimation(getCatStateName(state));
        b_stateTimer = 0.f;

        std::uniform_int_distribution<int> distTime(b_stateTimerMin, b_stateTimerMax);
        b_stateTimeMax = static_cast<float>(distTime(rng));

        updateDirection();

        if (state == BuddyState::Jump) {
            if (distanceTo(mouseX, mouseY, b_x, b_y) >= 50.f) {
                std::uniform_int_distribution<int> jitter(-50, 50);
                float targetX = mouseX + static_cast<float>(jitter(rng));
                float targetY = mouseY + static_cast<float>(jitter(rng));
                updateTrajectory(targetX, targetY, static_cast<float>(b_windowBounds.ground));
            } else {
                // Too close to jump — fall back to idle
                setAnimation("idle_0");
            }
        }

        if (state == BuddyState::Walk || state == BuddyState::Run) {
            if (std::abs(b_x - b_targetX) > 25.f) {
                std::uniform_int_distribution<int> isMouseTargetDist(0, 100);
                if (isMouseTargetDist(rng) < 50) {
                    b_targetX = mouseX;
                } else {
                    std::uniform_int_distribution<int> randomPositionDist(0, b_windowBounds.width);
                    b_targetX = static_cast<float>(randomPositionDist(rng));
                }
            }
        }
    }
    void Buddy::setPosition(float nx, float ny)                         { b_x = nx; b_y = ny; }
    void Buddy::setSpeed(float s)                                       { b_speed = s; }
    void Buddy::setScale(float s)                                       { b_scale = s; }
    void Buddy::setGroundY(float y)                                     { b_windowBounds.ground = y; }
    void Buddy::setFrameInterval(float ticks)                           { b_frameInterval = ticks; }
    void Buddy::setDirection(int dir)                                   { b_direction = (dir >= 0 ? 1 : -1); }
    void Buddy::setStateTimer(float ticks)                              { b_stateTimer = ticks; }
    void Buddy::setStateTimerMin(float ticks)                           { b_stateTimerMin = static_cast<int>(ticks); }
    void Buddy::setStateTimerMax(float ticks)                           { b_stateTimerMax = static_cast<int>(ticks); }
    void Buddy::setStateTimeMax(float ticks)                            { b_stateTimeMax = ticks; }

    float       Buddy::getX()             const { return b_x; }
    float       Buddy::getY()             const { return b_y; }
    std::string Buddy::getName()          const { return b_name; }
    std::string Buddy::getState()         const { return ByteBuddy::getCatStateName(b_state); }
    JumpPhase   Buddy::getJumpPhase()     const { return b_jumpPhase; }
    float       Buddy::getSpeed()         const { return b_speed; }
    float       Buddy::getScale()         const { return b_scale; }
    float       Buddy::getStateTimeMax()  const { return b_stateTimeMax; }
    int         Buddy::getStateTimerMax() const { return b_stateTimerMax; }
    int         Buddy::getStateTimerMin() const { return b_stateTimerMin; }
    float       Buddy::getFrameInterval() const { return b_frameInterval; }
    int         Buddy::getDirection()     const { return b_direction; }
    int         Buddy::getFrame()         const { return b_frame; }
    int         Buddy::getGroundY()       const { return b_windowBounds.ground; }

} // namespace ByteBuddy